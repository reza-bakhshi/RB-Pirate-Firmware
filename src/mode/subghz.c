#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/bio.h"
#include "pirate/cc1101.h"
#include "pirate/storage.h"
#include "ui/ui_help.h"
#include "ui/ui_file_picker.h"
#include "ui/ui_term.h"
#include "lib/bp_args/bp_cmd.h"
#include "usb_rx.h"
#include "fatfs/ff.h"
#include "mode/subghz.h"
#include "mode/subghz_protocols.h"
#include "mode/subghz_rx.h"
#include "mode/subghz_keystore.h"
#include "mode/subghz_file.h"
#include "mode/subghz_cap.h"
#include "mode/subghz_freqs.h"

#define SUBGHZ_DEFAULT_KHZ 433920u
#define SUBGHZ_MAX_HOP 8
#define SUBGHZ_KEY_FILE "keeloq_mfcodes"
#define SUBGHZ_HOP_FILE "subghz_hop.txt"

/* TX policy: strict = ISM segments only, open = any valid CC1101 band. */
#define SUBGHZ_TX_STRICT 0u
#define SUBGHZ_TX_OPEN 1u

static struct {
    uint32_t frequency_khz;
    uint32_t gdo_pin;
    uint32_t preset;           /* cc1101_preset_t */
    uint32_t bandwidth_khz;   /* Legacy: 270 or 650, used for OOK presets */
    uint32_t tx_power;        /* Legacy raw PA table value, 0 = default */
    int32_t tx_power_dbm;     /* dBm value, 127 = use default per band */
    uint32_t bitrate_baud;    /* 0 = use preset default */
    uint32_t tx_policy;       /* SUBGHZ_TX_STRICT or SUBGHZ_TX_OPEN */
    uint32_t hop_count;
    uint32_t hop_frequencies[SUBGHZ_MAX_HOP];
    uint32_t initialized;
} mode_config = {
    .frequency_khz = SUBGHZ_DEFAULT_KHZ,
    .gdo_pin = BIO0,
    .preset = CC1101_PRESET_OOK_270,
    .bandwidth_khz = 270,
    .tx_power = 0,
    .tx_power_dbm = 127,
    .bitrate_baud = 0,
    .tx_policy = SUBGHZ_TX_STRICT,
    .hop_count = 0,
};

/* Last decoded protocol frame (for savekey / protocol replay). */
static struct {
    bool valid;
    char protocol[SUBGHZ_PROTOCOL_NAME_MAX];
    uint64_t key;
    uint32_t bits;
    uint32_t te_us;
    uint32_t frequency_khz;
    uint32_t preset;
    char manufacture[24];
} last_decode;

/* Manufacturer keystore for KeeLoq simple-learning decrypt. */
static subghz_keystore_t keystore;
static bool keystore_loaded;

#define SUBGHZ_GDO0 ((uint8_t)mode_config.gdo_pin)

/* Positive durations are high pulses, negative durations are low pulses. */
static int32_t capture_buffer[1024];
static uint32_t capture_count;
static uint8_t analyzer_half_bits[2048];

static const char pin_labels[][5] = { "GDO0", "MISO", "CS", "SCK", "MOSI" };

static bool valid_frequency(uint32_t khz) {
    return (khz >= 300000u && khz <= 348000u) ||
           (khz >= 387000u && khz <= 464000u) ||
           (khz >= 779000u && khz <= 928000u);
}

static uint8_t frequency_band(uint32_t khz) {
    if (khz >= 300000u && khz <= 348000u) return 1;
    if (khz >= 387000u && khz <= 464000u) return 2;
    if (khz >= 779000u && khz <= 928000u) return 3;
    return 0;
}

static bool valid_frequency_range(uint32_t start, uint32_t stop) {
    uint8_t band = frequency_band(start);
    return band && band == frequency_band(stop) && start <= stop;
}

/* TX allow-list: ISM segments where short-range devices typically operate.
 * Strict policy blocks TX elsewhere even though the CC1101 can tune there. */
static bool tx_ism_allowed(uint32_t khz) {
    return (khz >= 314500u && khz <= 315500u) ||   /* 315 MHz SRD */
           (khz >= 433050u && khz <= 434790u) ||   /* 433 MHz ISM */
           (khz >= 868000u && khz <= 868600u) ||   /* 868 MHz SRD */
           (khz >= 902000u && khz <= 928000u);     /* 915 MHz ISM */
}

static bool tx_allowed_here(void) {
    if (mode_config.tx_policy == SUBGHZ_TX_OPEN) {
        return valid_frequency(mode_config.frequency_khz);
    }
    return tx_ism_allowed(mode_config.frequency_khz);
}

/* Guard every transmit path. Returns true when TX may proceed. */
static bool tx_guard(void) {
    if (!tx_allowed_here()) {
        printf("TX blocked: %u.%03u MHz is outside the allowed ISM segments.\r\n",
               mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
        printf("Allowed: 314.5-315.5, 433.05-434.79, 868.0-868.6, 902-928 MHz.\r\n");
        printf("Use 'txallow open' to override (obey local regulations).\r\n");
        return false;
    }
    return true;
}

static void apply_tx_power(void) {
    bool ook = cc1101_preset_is_ook((cc1101_preset_t)mode_config.preset);
    if (mode_config.tx_power_dbm != 127) {
        (void)cc1101_set_tx_power_dbm((int8_t)mode_config.tx_power_dbm, ook);
    } else if (mode_config.tx_power) {
        (void)cc1101_set_tx_power((uint8_t)mode_config.tx_power);
    }
}

static bool field_value_equals(const char *line, size_t prefix_length, const char *expected) {
    return subghz_field_value_equals(line, prefix_length, expected);
}

static bool parse_frequency_field(const char *line, uint32_t *frequency_khz) {
    return subghz_parse_frequency_khz(line, frequency_khz);
}

/* Parse "Bit: 24" (4). */
static bool parse_bit_field(const char *line, uint32_t *bits) {
    return subghz_parse_bit_field(line, bits);
}

/* Parse "Key: 00 00 ... A6 8D" hex bytes MSB-first into a uint64 (max 8 bytes). */
static bool parse_key_field(const char *line, uint64_t *key, uint32_t *nibbles) {
    return subghz_parse_key_field(line, key, nibbles);
}

/* Parse "TE: 350" (3). */
static bool parse_te_field(const char *line, uint32_t *te_us) {
    return subghz_parse_te_field(line, te_us);
}

/* Match a Flipper preset name to a CC1101 preset (OOK + FSK families). */
static bool preset_from_flipper_name(const char *line, size_t prefix, cc1101_preset_t *preset) {
    struct {
        const char *name;
        cc1101_preset_t preset;
    } table[] = {
        { "FuriHalSubGhzPresetOok270Async", CC1101_PRESET_OOK_270 },
        { "FuriHalSubGhzPresetOok650Async", CC1101_PRESET_OOK_650 },
        { "FuriHalSubGhzPreset2FSKDev238Async", CC1101_PRESET_2FSK_DEV238 },
        { "FuriHalSubGhzPreset2FSKDev12KAsync", CC1101_PRESET_2FSK_DEV12K },
        { "FuriHalSubGhzPreset2FSKDev476Async", CC1101_PRESET_2FSK_DEV476 },
        { "FuriHalSubGhzPresetGFSK9_99KbAsync", CC1101_PRESET_GFSK_10K },
        { "FuriHalSubGhzPresetMSK99_97KbAsync", CC1101_PRESET_MSK_100K },
        { "Ook270Async", CC1101_PRESET_OOK_270 },
        { "Ook650Async", CC1101_PRESET_OOK_650 },
        { "2FSKDev238", CC1101_PRESET_2FSK_DEV238 },
        { "2FSKDev12K", CC1101_PRESET_2FSK_DEV12K },
        { "2FSKDev476", CC1101_PRESET_2FSK_DEV476 },
        { "GFSK10K", CC1101_PRESET_GFSK_10K },
        { "MSK100K", CC1101_PRESET_MSK_100K },
        { "Custom", CC1101_PRESET_CUSTOM },
        { "FuriHalSubGhzPresetCustom", CC1101_PRESET_CUSTOM },
        { "AM270", CC1101_PRESET_OOK_270 },
        { "AM650", CC1101_PRESET_OOK_650 },
    };
    for (uint i = 0; i < count_of(table); i++) {
        if (field_value_equals(line, prefix, table[i].name)) {
            *preset = table[i].preset;
            return true;
        }
    }
    return false;
}

static void frequency_band_bounds(uint32_t khz, uint32_t *low, uint32_t *high) {
    if (khz <= 348000u) { *low = 300000u; *high = 348000u; }
    else if (khz <= 464000u) { *low = 387000u; *high = 464000u; }
    else { *low = 779000u; *high = 928000u; }
}

/* ============================================================
 * Command definitions
 * ============================================================ */

static const bp_val_constraint_t frequency_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 300000, .max = 928000, .def = SUBGHZ_DEFAULT_KHZ },
};
static const bp_val_constraint_t gdo_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = BIO0, .max = BIO3, .def = BIO0 },
};
static const bp_val_constraint_t bandwidth_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = (const bp_val_choice_t[]){
        { "270", NULL, 0, 270 },
        { "650", NULL, 0, 650 },
    }, .count = 2, .def = 0 },
};
static const bp_val_constraint_t preset_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = (const bp_val_choice_t[]){
        { "Ook270Async",   NULL, 0, CC1101_PRESET_OOK_270 },
        { "Ook650Async",   NULL, 0, CC1101_PRESET_OOK_650 },
        { "2FSKDev238",    NULL, 0, CC1101_PRESET_2FSK_DEV238 },
        { "2FSKDev12K",    NULL, 0, CC1101_PRESET_2FSK_DEV12K },
        { "2FSKDev476",    NULL, 0, CC1101_PRESET_2FSK_DEV476 },
        { "GFSK10K",       NULL, 0, CC1101_PRESET_GFSK_10K },
        { "MSK100K",       NULL, 0, CC1101_PRESET_MSK_100K },
        { "Custom",        NULL, 0, CC1101_PRESET_CUSTOM },
    }, .count = 8, .def = 0 },
};

static const bp_command_opt_t setup_opts[] = {
    { "frequency", 'f', BP_ARG_REQUIRED, "kHz", 0, &frequency_range },
    { "gdo", 'g', BP_ARG_REQUIRED, "BIO0-BIO3", 0, &gdo_range },
    { "preset", 'p', BP_ARG_REQUIRED, "Ook270Async|Ook650Async|...", 0, &preset_choice },
    { 0 }
};
const bp_command_def_t subghz_setup_def = {
    .name = "subghz", .description = 0,
    .description_text = "Configure and enter CC1101 Sub-GHz mode",
    .opts = setup_opts,
};

static const bp_command_positional_t frequency_positionals[] = {
    { "kHz", "300000-928000 (supported CC1101 bands)", 0, true, NULL, "tuning frequency in kHz" }, { 0 }
};
static const char *const frequency_usage[] = {
    "setfrequency <kHz>",
    "Tune to 433.92 MHz:%s setfrequency 433920",
    "Tune to 315.00 MHz:%s setfrequency 315000",
    "Tune to 868.35 MHz:%s setfrequency 868350",
};
static const bp_command_def_t frequency_def = {
    .name = "setfrequency", .description = 0,
    .description_text = "Tune CC1101 radio to frequency (in kHz)",
    .positionals = frequency_positionals, .positional_count = 1,
    .usage = frequency_usage, .usage_count = count_of(frequency_usage),
};

static const bp_command_opt_t rssi_opts[] = {
    { "dwell", 'd', BP_ARG_REQUIRED, "ms", 0, NULL, "hold time in ms for peak RSSI measurement" }, { 0 }
};
static const char *const rssi_usage[] = {
    "rssi [-d dwell_ms]",
    "Instantaneous RSSI:%s rssi",
    "Peak RSSI over 100ms:%s rssi -d 100",
};
static const bp_command_def_t rssi_def = {
    .name = "rssi", .description = 0,
    .description_text = "Read signal strength (instantaneous or peak held)",
    .opts = rssi_opts,
    .usage = rssi_usage, .usage_count = count_of(rssi_usage),
};

static const bp_command_opt_t scan_opts[] = {
    { "start", 'a', BP_ARG_REQUIRED, "kHz", 0, NULL, "start frequency in kHz" },
    { "stop",  'z', BP_ARG_REQUIRED, "kHz", 0, NULL, "stop frequency in kHz" },
    { "step",  's', BP_ARG_REQUIRED, "kHz", 0, NULL, "step size in kHz" },
    { "dwell", 'd', BP_ARG_REQUIRED, "ms", 0, NULL, "dwell time in ms per step" },
    { "band",  'b', BP_ARG_REQUIRED, "all|300|433|868", 0, NULL, "standard band channels" },
    { "list",  'l', BP_ARG_NONE,     NULL, 0, NULL, "list channel frequencies" }, { 0 }
};
static const char *const scan_usage[] = {
    "scan [-a start_kHz] [-z stop_kHz] [-s step_kHz] [-d dwell_ms]",
    "scan -b [all|300|433|868] [-d dwell_ms]",
    "scan -l",
    "Scan around current frequency:%s scan",
    "Scan 433 MHz ISM band channels:%s scan -b 433 -d 10",
    "Scan custom range:%s scan -a 433050 -z 434790 -s 25 -d 10",
    "List all channels by band:%s scan -l",
};
static const bp_command_def_t scan_def = {
    .name = "scan", .description = 0,
    .description_text = "Scan frequency range or discrete channels for activity",
    .opts = scan_opts,
    .usage = scan_usage, .usage_count = count_of(scan_usage),
};

static const bp_command_opt_t receive_opts[] = {
    { "time",    't', BP_ARG_REQUIRED, "ms", 0, NULL, "capture duration in ms (default 5000)" },
    { "verbose", 'v', BP_ARG_NONE,     NULL, 0, NULL, "display formatted 4-column raw pulse dump" }, { 0 }
};
static const char *const receive_usage[] = {
    "receive [-t milliseconds] [-v]",
    "Capture GDO0 pulse widths for 5 seconds:%s receive",
    "Capture and display formatted pulses:%s receive -t 2000 -v",
};
static const bp_command_def_t receive_def = {
    .name = "receive", .description = 0,
    .description_text = "Capture raw GDO0 pulse train and run protocol decoders",
    .opts = receive_opts,
    .usage = receive_usage, .usage_count = count_of(receive_usage),
};

static const bp_command_opt_t sweep_opts[] = {
    { "start",     'a', BP_ARG_REQUIRED, "kHz", 0, NULL, "start frequency in kHz" },
    { "stop",      'z', BP_ARG_REQUIRED, "kHz", 0, NULL, "stop frequency in kHz" },
    { "step",      's', BP_ARG_REQUIRED, "kHz", 0, NULL, "step size in kHz" },
    { "samples",   'n', BP_ARG_REQUIRED, "2-100", 0, NULL, "samples per frequency" },
    { "threshold", 'r', BP_ARG_REQUIRED, "dBm", 0, NULL, "positive dBm threshold magnitude" }, { 0 }
};
static const char *const sweep_usage[] = {
    "sweep [-a start_kHz] [-z stop_kHz] [-s step_kHz] [-n samples] [-r threshold]",
    "Sweep 433MHz with 20 samples per step:%s sweep -a 433050 -z 434790 -s 25 -n 20 -r 85",
};
static const bp_command_def_t sweep_def = {
    .name = "sweep", .description = 0,
    .description_text = "Multi-sample signal detection across frequency range",
    .opts = sweep_opts,
    .usage = sweep_usage, .usage_count = count_of(sweep_usage),
};

static const bp_command_opt_t trace_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "ms", 0, NULL, "duration in ms (100-10000)" }, { 0 }
};
static const char *const trace_usage[] = {
    "trace [-t milliseconds]",
    "Sample GDO0 logic levels for 2 seconds:%s trace -t 2000",
};
static const bp_command_def_t trace_def = {
    .name = "trace", .description = 0,
    .description_text = "Terminal logic-analyzer trace of GDO0 pulse levels",
    .opts = trace_opts,
    .usage = trace_usage, .usage_count = count_of(trace_usage),
};

static const bp_command_opt_t waterfall_opts[] = {
    { "start", 'a', BP_ARG_REQUIRED, "kHz", 0, NULL, "start frequency in kHz" },
    { "stop",  'z', BP_ARG_REQUIRED, "kHz", 0, NULL, "stop frequency in kHz" },
    { "step",  's', BP_ARG_REQUIRED, "kHz", 0, NULL, "step size in kHz" },
    { "rows",  'n', BP_ARG_REQUIRED, "1-100", 0, NULL, "number of rows" }, { 0 }
};
static const char *const waterfall_usage[] = {
    "waterfall [-a start_kHz] [-z stop_kHz] [-s step_kHz] [-n rows]",
    "Display 20 rows across 433 MHz band:%s waterfall -a 433050 -z 434790 -s 25 -n 20",
};
static const bp_command_def_t waterfall_def = {
    .name = "waterfall", .description = 0,
    .description_text = "ASCII waterfall display of signal strength over time",
    .opts = waterfall_opts,
    .usage = waterfall_usage, .usage_count = count_of(waterfall_usage),
};

static const bp_command_opt_t replay_opts[] = {
    { "repeat", 'r', BP_ARG_REQUIRED, "1-20", 0, NULL, "number of retransmissions" }, { 0 }
};
static const char *const replay_usage[] = {
    "replay [-r repeats]",
    "Transmit last captured pulses once:%s replay",
    "Transmit last captured pulses 3 times:%s replay -r 3",
};
static const bp_command_def_t replay_def = {
    .name = "replay", .description = 0,
    .description_text = "Replay last captured or loaded pulse buffer via CC1101 TX",
    .opts = replay_opts,
    .usage = replay_usage, .usage_count = count_of(replay_usage),
};

static const bp_command_positional_t record_positionals[] = {
    { "file", "output .sub file", 0, true, NULL, "destination .sub file path" }, { 0 }
};
static const bp_command_opt_t record_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "ms", 0, NULL, "recording duration in ms" }, { 0 }
};
static const char *const record_usage[] = {
    "record <file.sub> [-t milliseconds]",
    "Record raw pulses for 5 seconds to file:%s record capture.sub -t 5000",
};
static const bp_command_def_t record_def = {
    .name = "record", .description = 0,
    .description_text = "Record raw pulse stream directly to Flipper .sub file",
    .opts = record_opts,
    .positionals = record_positionals, .positional_count = 1,
    .usage = record_usage, .usage_count = count_of(record_usage),
};

static const bp_command_positional_t load_positionals[] = {
    { "file", "Flipper RAW .sub file", 0, false, NULL, "path to .sub file" }, { 0 }
};
static const bp_command_opt_t load_opts[] = {
    { "repeat", 'r', BP_ARG_REQUIRED, "0-20", 0, NULL, "retransmit count (0 = load only)" }, { 0 }
};
static const char *const load_usage[] = {
    "load <file.sub> [-r repeats]",
    "Load without transmitting:%s load capture.sub",
    "Load and replay twice:%s load capture.sub -r 2",
    "Replay loaded buffer 5 times:%s load -r 5",
};
static const bp_command_def_t load_def = {
    .name = "load", .description = 0,
    .description_text = "Load and parse Flipper RAW or protocol key file (.sub)",
    .opts = load_opts,
    .positionals = load_positionals, .positional_count = 1,
    .usage = load_usage, .usage_count = count_of(load_usage),
};

static const bp_command_positional_t decode_positionals[] = {
    { "file", ".sub file to inspect (no TX)", 0, false, NULL, "file path to inspect" }, { 0 }
};
static const char *const decode_usage[] = {
    "decode <file.sub>",
    "Inspect a capture without transmitting:%s decode capture.sub",
};
static const bp_command_def_t decode_def = {
    .name = "decode", .description = 0,
    .description_text = "Inspect and decode a .sub file without transmitting",
    .positionals = decode_positionals, .positional_count = 1,
    .usage = decode_usage, .usage_count = count_of(decode_usage),
};

static const bp_command_positional_t clone_positionals[] = {
    { "file", "output .sub file (auto-named if omitted)", 0, false, NULL, "destination .sub file" }, { 0 }
};
static const bp_command_opt_t clone_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "ms", 0, NULL, "capture duration in ms (default 5000)" }, { 0 }
};
static const char *const clone_usage[] = {
    "clone [file.sub] [-t milliseconds]",
    "Capture, decode, and save in one step:%s clone",
    "Capture and save with specific filename:%s clone myremote.sub -t 8000",
};
static const bp_command_def_t clone_def = {
    .name = "clone", .description = 0,
    .description_text = "Guided capture, decode, and save key file in one step",
    .opts = clone_opts,
    .positionals = clone_positionals, .positional_count = 1,
    .usage = clone_usage, .usage_count = count_of(clone_usage),
};

static const bp_command_positional_t savekey_positionals[] = {
    { "file", "output key .sub file", 0, true, NULL, "destination .sub file path" }, { 0 }
};
static const char *const savekey_usage[] = {
    "savekey <file.sub>",
    "Save the last decoded frame:%s savekey remote.sub",
};
static const bp_command_def_t savekey_def = {
    .name = "savekey", .description = 0,
    .description_text = "Save last decoded frame as a Flipper protocol key file",
    .positionals = savekey_positionals, .positional_count = 1,
    .usage = savekey_usage, .usage_count = count_of(savekey_usage),
};

static const bp_command_opt_t monitor_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "ms", 0, NULL, "monitoring duration in ms" }, { 0 }
};
static const char *const monitor_usage[] = {
    "monitor [-t milliseconds]",
    "Live-decode continuously until keypress:%s monitor",
    "Live-decode for 10 seconds:%s monitor -t 10000",
};
static const bp_command_def_t monitor_def = {
    .name = "monitor", .description = 0,
    .description_text = "Continuous live protocol decoding with deduplication",
    .opts = monitor_opts,
    .usage = monitor_usage, .usage_count = count_of(monitor_usage),
};

static const bp_command_positional_t keyload_positionals[] = {
    { "file", "key file (default keeloq_mfcodes)", 0, false, NULL, "manufacturer keys file" }, { 0 }
};
static const char *const keyload_usage[] = {
    "keyload [file]",
    "Load default keeloq_mfcodes file:%s keyload",
    "Load specific keys file:%s keyload custom_keys.txt",
};
static const bp_command_def_t keyload_def = {
    .name = "keyload", .description = 0,
    .description_text = "Load KeeLoq manufacturer keys file (keeloq_mfcodes)",
    .positionals = keyload_positionals, .positional_count = 1,
    .usage = keyload_usage, .usage_count = count_of(keyload_usage),
};

static const char *const keys_usage[] = {
    "keys",
    "Display all loaded manufacturer keys and types:%s keys",
};
static const bp_command_def_t keys_def = {
    .name = "keys", .description = 0,
    .description_text = "List loaded KeeLoq manufacturer decryption keys",
    .usage = keys_usage, .usage_count = count_of(keys_usage),
};

static const bp_command_positional_t keyadd_positionals[] = {
    { "name", "manufacturer name", 0, true, NULL, "manufacturer identifier" },
    { "key",  "64-bit hex key",    0, true, NULL, "64-bit hex manufacturer key" },
    { "type", "learning type",     0, false, NULL, "0=unk 1=simple 2=normal 3=secure 4-7=magic" }, { 0 }
};
static const char *const keyadd_usage[] = {
    "keyadd <name> <hexkey> [type]",
    "Add normal-learning key:%s keyadd MyBrand 0102030405060708 2",
};
static const bp_command_def_t keyadd_def = {
    .name = "keyadd", .description = 0,
    .description_text = "Add custom KeeLoq manufacturer key to live keystore",
    .positionals = keyadd_positionals, .positional_count = 3,
    .usage = keyadd_usage, .usage_count = count_of(keyadd_usage),
};

static const bp_command_positional_t send_positionals[] = {
    { "payload", "integer/hex value, or hex key with -P", 0, true, NULL, "payload value or key" }, { 0 }
};
static const bp_command_opt_t send_opts[] = {
    { "bits",     'b', BP_ARG_REQUIRED, "1-64", 0, NULL, "number of bits to transmit" },
    { "te",       't', BP_ARG_REQUIRED, "us",   0, NULL, "elementary pulse time TE in us" },
    { "repeat",   'r', BP_ARG_REQUIRED, "1-20", 0, NULL, "retransmission count" },
    { "protocol", 'P', BP_ARG_REQUIRED, "name", 0, NULL, "protocol name to encode" },
    { "key",      'k', BP_ARG_REQUIRED, "hex",  0, NULL, "hex key data (with -P)" },
    { "proto",    'o', BP_ARG_REQUIRED, "1|2|11", 0, NULL, "sub-protocol (for RcSwitch)" },
    { "burst",    'R', BP_ARG_NONE,     NULL,   0, NULL, "transmit random RF jitter noise burst" }, { 0 }
};
static const char *const send_usage[] = {
    "send <payload> [-b bits] [-t TE_us] [-r repeats]",
    "send -P <protocol> -k <hexkey> [-b bits] [-t TE_us] [-r repeats] [-o subproto]",
    "send -R",
    "Send a 24-bit Princeton frame:%s send 0x123456 -b 24 -t 350 -r 4",
    "Send a GateTX key:%s send -P GateTX -k 0xABCDEF -b 24 -r 10",
    "Send an RcSwitch key (proto 1/2/11):%s send -P RcSwitch -k 0x123456 -b 24 -o 2",
    "Send a random noise burst:%s send -R",
};
static const bp_command_def_t send_def = {
    .name = "send", .description = 0,
    .description_text = "Transmit protocol frame, raw Princeton payload, or burst",
    .opts = send_opts,
    .positionals = send_positionals, .positional_count = 1,
    .usage = send_usage, .usage_count = count_of(send_usage),
};

static const bp_command_opt_t burst_opts[] = {
    { "count",     'c', BP_ARG_REQUIRED, "1-100", 0, NULL, "number of bursts (default 1)" },
    { "frequency", 'f', BP_ARG_REQUIRED, "kHz",   0, NULL, "frequency in kHz (default current)" },
    { 0 }
};
static const char *const burst_usage[] = {
    "burst [-c count] [-f kHz]",
    "Send a random RF jitter burst:%s burst",
    "Send 5 bursts at 433.92 MHz:%s burst -c 5 -f 433920",
};
static const bp_command_def_t burst_def = {
    .name = "burst", .description = 0,
    .description_text = "Transmit pseudo-random RF jitter pulse bursts for testing",
    .opts = burst_opts,
    .usage = burst_usage, .usage_count = count_of(burst_usage),
};

/* --- New commands --- */

static const bp_command_positional_t preset_positionals[] = {
    { "name", "preset name, import, or export", 0, true, NULL, "preset name or action" },
    { "file", "file for import/export", 0, false, NULL, "file path on storage" }, { 0 }
};
static const char *const preset_usage[] = {
    "preset [name] [file]",
    "List all available presets:%s preset",
    "Switch to OOK 650kHz:%s preset Ook650Async",
    "Import custom registers:%s preset import custom.sub",
    "Export current registers:%s preset export my.sub",
};
static const bp_command_def_t preset_def = {
    .name = "preset", .description = 0,
    .description_text = "Select, import, or export CC1101 modulation presets",
    .positionals = preset_positionals, .positional_count = 2,
    .usage = preset_usage, .usage_count = count_of(preset_usage),
};

static const bp_command_positional_t protocol_positionals[] = {
    { "name", "protocol name (list with bare 'protocol')", 0, true, NULL, "protocol to inspect" }, { 0 }
};
static const char *const protocol_usage[] = {
    "protocol [name]",
    "List all protocols:%s protocol",
    "Decode Princeton:%s protocol Princeton",
    "Decode GateTX:%s protocol GateTX",
};
static const bp_command_def_t protocol_def = {
    .name = "protocol", .description = 0,
    .description_text = "List supported protocols or display protocol details",
    .positionals = protocol_positionals, .positional_count = 1,
    .usage = protocol_usage, .usage_count = count_of(protocol_usage),
};

static const bp_command_opt_t hop_opts[] = {
    { "frequencies", 'f', BP_ARG_REQUIRED, "kHz1,kHz2,...", 0, NULL, "comma-separated frequencies" },
    { "dwell",       'd', BP_ARG_REQUIRED, "ms",            0, NULL, "dwell time in ms per frequency" },
    { "save",        's', BP_ARG_NONE,     NULL,            0, NULL, "save hop list to subghz_hop.txt" },
    { 0 }
};
static const char *const hop_usage[] = {
    "hop -f freq1,freq2,... [-d dwell_ms] [-s]",
    "Hop between 433.92 and 315.00 MHz:%s hop -f 433920,315000 -d 500",
    "Hop and persist list:%s hop -f 433920,868350,315000 -d 250 -s",
};
static const bp_command_def_t hop_def = {
    .name = "hop", .description = 0,
    .description_text = "Hop across multiple frequencies monitoring for activity",
    .opts = hop_opts,
    .usage = hop_usage, .usage_count = count_of(hop_usage),
};

static const bp_command_opt_t power_opts[] = {
    { "dbm",   'd', BP_ARG_REQUIRED, "-30..12", 0, NULL, "output power in dBm" },
    { "level", 'l', BP_ARG_REQUIRED, "0-255",   0, NULL, "raw PATABLE byte (legacy)" },
    { 0 }
};
static const char *const power_usage[] = {
    "power [-d dBm] [-l raw]",
    "Set 0 dBm output:%s power -d 0",
    "Set +10 dBm output:%s power -d 10",
    "Maximum +12 dBm (check local limits):%s power -d 12",
};
static const bp_command_def_t power_def = {
    .name = "power", .description = 0,
    .description_text = "Set CC1101 transmit output power (in dBm or raw)",
    .opts = power_opts,
    .usage = power_usage, .usage_count = count_of(power_usage),
};

static const bp_command_positional_t txallow_positionals[] = {
    { "policy", "strict|open", 0, false, NULL, "strict (ISM only) or open (all frequencies)" }, { 0 }
};
static const char *const txallow_usage[] = {
    "txallow [strict|open]",
    "Display current transmit policy:%s txallow",
    "Enforce strict ISM-band rules (default):%s txallow strict",
    "Allow transmission on all tunable frequencies:%s txallow open",
};
static const bp_command_def_t txallow_def = {
    .name = "txallow", .description = 0,
    .description_text = "Configure transmit policy (strict ISM bands or open)",
    .positionals = txallow_positionals, .positional_count = 1,
    .usage = txallow_usage, .usage_count = count_of(txallow_usage),
};

static const bp_command_opt_t bitrate_opts[] = {
    { "baud", 'b', BP_ARG_REQUIRED, "600-500000", 0, NULL, "data rate in baud" }, { 0 }
};
static const char *const bitrate_usage[] = {
    "bitrate [-b baud]",
    "Set 4800 baud data rate:%s bitrate -b 4800",
    "Set 38400 baud data rate:%s bitrate -b 38400",
};
static const bp_command_def_t bitrate_def = {
    .name = "bitrate", .description = 0,
    .description_text = "Configure CC1101 data rate (600-500000 baud)",
    .opts = bitrate_opts,
    .usage = bitrate_usage, .usage_count = count_of(bitrate_usage),
};

static const char *const info_usage[] = {
    "info",
    "Show frequency, preset, modulation, power, and RSSI:%s info",
};
static const bp_command_def_t info_def = {
    .name = "info", .description = 0,
    .description_text = "Display CC1101 radio configuration and status",
    .usage = info_usage, .usage_count = count_of(info_usage),
};

/* ============================================================
 * Command helpers
 * ============================================================ */

static bool command_ready(void) {
    if (mode_config.initialized) return true;
    printf("CC1101 is not initialized; check wiring and use 3.3V power.\r\n");
    return false;
}

static void pulse(bool level, uint32_t us);
static void load_file(const char *name, uint32_t repeat);
static bool transmit_upload(subghz_protocol_encoder_t *enc, uint32_t repeat);
static bool parse_custom_preset(const char *line, uint8_t *pairs, size_t *pair_count);
static bool parse_custom_preset_full(const char *line, uint8_t *pairs, size_t *pair_count,
                                     uint8_t *pa_table, bool *have_pa);
static const char *preset_to_flipper_name(cc1101_preset_t preset);

static void setfrequency_handler(struct command_result *res) {
    if (bp_cmd_help_check(&frequency_def, res->help_flag)) return;
    uint32_t khz;
    if (!bp_cmd_get_positional_uint32(&frequency_def, 1, &khz) || !valid_frequency(khz)) {
        printf("Invalid frequency. Supported: 300-348, 387-464, and 779-928 MHz.\r\n");
        return;
    }
    if (!command_ready() || !cc1101_set_frequency(khz)) return;
    if (!cc1101_rx()) { printf("Failed to enter RX state.\r\n"); return; }
    mode_config.frequency_khz = khz;
    printf("Frequency: %u.%03u MHz\r\n", khz / 1000, khz % 1000);
}

static void rssi_handler(struct command_result *res) {
    if (bp_cmd_help_check(&rssi_def, res->help_flag) || !command_ready()) return;
    uint32_t dwell = 0;
    if (bp_cmd_get_uint32(&rssi_def, 'd', &dwell) && dwell > 0) {
        if (dwell > 10000) dwell = 10000;
        int16_t current = cc1101_rssi();
        int16_t peak = -127;
        absolute_time_t until = make_timeout_time_ms(dwell);
        while (!time_reached(until)) {
            int16_t sample = cc1101_rssi();
            if (sample > peak) peak = sample;
            sleep_ms(1);
        }
        printf("RSSI: %d dBm (Peak over %lu ms: %d dBm)\r\n", current, (unsigned long)dwell, peak);
    } else {
        printf("RSSI: %d dBm\r\n", cc1101_rssi());
    }
}

static void scan_handler(struct command_result *res) {
    if (bp_cmd_help_check(&scan_def, res->help_flag) || !command_ready()) return;

    if (bp_cmd_find_flag(&scan_def, 'l')) {
        printf("Supported SubGHz frequency bands and channels (from SubGhzService):\r\n");
        for (int b = 1; b < SUBGHZ_BAND_COUNT; b++) {
            size_t count = 0;
            const uint32_t *freqs = subghz_band_frequencies((subghz_scan_band_t)b, &count);
            printf("  Band %s (%zu channels):\r\n   ", subghz_scan_band_names[b], count);
            for (size_t i = 0; i < count; i++) {
                printf(" %u.%03u", freqs[i] / 1000, freqs[i] % 1000);
                if ((i + 1) % 6 == 0 && i + 1 < count) printf("\r\n   ");
            }
            printf("\r\n\r\n");
        }
        return;
    }

    char band_name[32] = { 0 };
    if (bp_cmd_get_string(&scan_def, 'b', band_name, sizeof(band_name))) {
        subghz_scan_band_t band = subghz_band_from_name(band_name);
        size_t count = 0;
        const uint32_t *freqs = subghz_band_frequencies(band, &count);
        if (!freqs || count == 0) {
            printf("Unknown band '%s'. Use all, 300, 433, or 868.\r\n", band_name);
            return;
        }
        uint32_t dwell = 10;
        if (bp_cmd_get_uint32(&scan_def, 'd', &dwell)) {
            if (!dwell || dwell > 1000) dwell = 10;
        }
        printf("Scanning %s (%zu channels, %lums dwell per channel)...\r\n",
               subghz_scan_band_names[band], count, (unsigned long)dwell);
        printf("Frequency (MHz)  Peak RSSI (dBm)\r\n");
        int16_t best = -127;
        uint32_t best_khz = freqs[0];
        for (size_t i = 0; i < count; i++) {
            uint32_t khz = freqs[i];
            if (!cc1101_set_frequency(khz) || !cc1101_rx()) continue;
            sleep_ms(2);
            int16_t peak = -127;
            absolute_time_t until = make_timeout_time_ms(dwell);
            while (!time_reached(until)) {
                int16_t s = cc1101_rssi();
                if (s > peak) peak = s;
                sleep_ms(1);
            }
            printf("%u.%03u          %d\r\n", khz / 1000, khz % 1000, peak);
            if (peak > best) {
                best = peak;
                best_khz = khz;
            }
            char c;
            if (rx_fifo_try_get(&c)) break;
        }
        if (best > -127) {
            if (cc1101_set_frequency(best_khz) && cc1101_rx()) {
                mode_config.frequency_khz = best_khz;
                printf("Selected strongest: %u.%03u MHz, %d dBm\r\n",
                       best_khz / 1000, best_khz % 1000, best);
            }
        } else {
            (void)cc1101_set_frequency(mode_config.frequency_khz);
            (void)cc1101_rx();
        }
        return;
    }
    uint32_t band_low, band_high;
    frequency_band_bounds(mode_config.frequency_khz, &band_low, &band_high);
    uint32_t start = mode_config.frequency_khz > band_low + 2000 ? mode_config.frequency_khz - 2000 : band_low;
    uint32_t stop = mode_config.frequency_khz < band_high - 2000 ? mode_config.frequency_khz + 2000 : band_high;
    uint32_t step = 25, dwell = 10, value;
    if (bp_cmd_get_uint32(&scan_def, 'a', &value)) start = value;
    if (bp_cmd_get_uint32(&scan_def, 'z', &value)) stop = value;
    if (bp_cmd_get_uint32(&scan_def, 's', &value)) step = value;
    if (bp_cmd_get_uint32(&scan_def, 'd', &value)) dwell = value;
    uint32_t points = step && stop >= start ? ((stop - start) / step) + 1 : 0;
    if (!step || step > 1000 || !dwell || dwell > 1000 || !valid_frequency_range(start, stop) ||
        points > 2000) {
        printf("Invalid range (step 1-1000kHz, dwell 1-1000ms, maximum 2000 points).\r\n");
        return;
    }
    int16_t best = -127;
    uint32_t best_khz = start;
    printf("Frequency (MHz)  Peak RSSI (dBm)\r\n");
    for (uint32_t khz = start; khz <= stop; khz += step) {
        if (!cc1101_set_frequency(khz) || !cc1101_rx()) {
            printf("Radio state transition failed during scan.\r\n");
            (void)cc1101_set_frequency(mode_config.frequency_khz);
            (void)cc1101_rx();
            return;
        }
        sleep_ms(2);
        int16_t peak = -127;
        absolute_time_t until = make_timeout_time_ms(dwell);
        while (!time_reached(until)) {
            int16_t sample = cc1101_rssi();
            if (sample > peak) peak = sample;
            sleep_ms(1);
        }
        printf("%u.%03u          %d\r\n", khz / 1000, khz % 1000, peak);
        if (peak > best) { best = peak; best_khz = khz; }
        char c;
        if (rx_fifo_try_get(&c)) break;
        if (UINT32_MAX - khz < step) break;
    }
    if (!cc1101_set_frequency(best_khz) || !cc1101_rx()) {
        printf("Unable to select the strongest frequency.\r\n");
        return;
    }
    mode_config.frequency_khz = best_khz;
    printf("Selected strongest: %u.%03u MHz, %d dBm\r\n",
           best_khz / 1000, best_khz % 1000, best);
}

static bool capture_pulses(uint32_t duration_ms) {
    capture_count = 0;
    if (!cc1101_rx()) {
        printf("Failed to enter RX state.\r\n");
        return false;
    }
    /* IRQ-timestamped edges: accurate even for short pulses. */
    subghz_cap_start(SUBGHZ_GDO0);
    bool started = false; /* Flipper RAW data starts with a high duration. */
    absolute_time_t until = make_timeout_time_ms(duration_ms);
    while (!time_reached(until) && capture_count < count_of(capture_buffer)) {
        bool level;
        uint32_t dur;
        while (subghz_cap_poll(&level, &dur) &&
               capture_count < count_of(capture_buffer)) {
            if (!dur) {
                continue;
            }
            if (!started) {
                if (!level) {
                    continue; /* Discard leading idle-low. */
                }
                started = true;
            }
            capture_buffer[capture_count] = level ? (int32_t)dur : -(int32_t)dur;
            capture_count++;
        }
        char c;
        if (rx_fifo_try_get(&c)) break;
        tight_loop_contents();
    }
    subghz_cap_stop();
    uint32_t overruns = subghz_cap_overruns();
    if (overruns) {
        printf("Warning: capture overrun (%lu edges dropped).\r\n", (unsigned long)overruns);
    }
    return capture_count > 0;
}

/* Frequency hopping capture: cycle through multiple frequencies. */
static bool capture_pulses_hop(uint32_t dwell_ms) {
    capture_count = 0;
    subghz_cap_start(SUBGHZ_GDO0);

    printf("Hopping capture across %lu frequencies, %lu ms dwell (press key to stop)...\r\n",
           (unsigned long)mode_config.hop_count, (unsigned long)dwell_ms);

    for (uint32_t h = 0; h < mode_config.hop_count && capture_count < count_of(capture_buffer); h++) {
        uint32_t khz = mode_config.hop_frequencies[h];
        if (!cc1101_set_frequency(khz) || !cc1101_rx()) continue;
        printf("  Hop %lu/%lu: %u.%03u MHz\r\n", (unsigned long)(h + 1),
               (unsigned long)mode_config.hop_count, khz / 1000, khz % 1000);

        bool started = false;
        absolute_time_t until = make_timeout_time_ms(dwell_ms);
        while (!time_reached(until) && capture_count < count_of(capture_buffer)) {
            bool level;
            uint32_t dur;
            while (subghz_cap_poll(&level, &dur) &&
                   capture_count < count_of(capture_buffer)) {
                if (!dur) {
                    continue;
                }
                if (!started) {
                    if (!level) {
                        continue;
                    }
                    started = true;
                }
                capture_buffer[capture_count] = level ? (int32_t)dur : -(int32_t)dur;
                capture_count++;
            }
            char c;
            if (rx_fifo_try_get(&c)) goto hop_done;
            tight_loop_contents();
        }
    }
hop_done:
    subghz_cap_stop();
    /* Restore configured frequency. */
    (void)cc1101_set_frequency(mode_config.frequency_khz);
    (void)cc1101_rx();
    return capture_count > 0;
}

static uint32_t pulse_duration(uint32_t index) {
    int32_t value = capture_buffer[index];
    return (uint32_t)(value < 0 ? -value : value);
}

static void analyze_capture(void) {
    if (capture_count < 4) {
        printf("Analysis: not enough pulses.\r\n");
        return;
    }

    uint32_t minimum = UINT32_MAX, maximum = 0, usable = 0;
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t duration = pulse_duration(i);
        if (duration < 20 || duration > 10000) continue; /* Ignore glitches and frame gaps. */
        if (duration < minimum) minimum = duration;
        if (duration > maximum) maximum = duration;
        usable++;
    }
    if (usable < 4 || minimum == UINT32_MAX) {
        printf("Analysis: no usable pulse train.\r\n");
        return;
    }

    /* Two-cluster integer k-means is robust against jitter and asymmetric distributions. */
    uint32_t short_us = minimum, long_us = maximum;
    for (uint32_t iteration = 0; iteration < 8; iteration++) {
        uint64_t short_total = 0, long_total = 0;
        uint32_t short_count = 0, long_count = 0;
        for (uint32_t i = 0; i < capture_count; i++) {
            uint32_t duration = pulse_duration(i);
            if (duration < 20 || duration > 10000) continue;
            uint32_t ds = duration > short_us ? duration - short_us : short_us - duration;
            uint32_t dl = duration > long_us ? duration - long_us : long_us - duration;
            if (ds <= dl) { short_total += duration; short_count++; }
            else { long_total += duration; long_count++; }
        }
        if (short_count) short_us = (uint32_t)(short_total / short_count);
        if (long_count) long_us = (uint32_t)(long_total / long_count);
    }
    if (short_us > long_us) { uint32_t swap = short_us; short_us = long_us; long_us = swap; }
    uint32_t split = short_us + (long_us - short_us) / 2;

    uint64_t short_error = 0, long_error = 0;
    uint32_t short_count = 0, long_count = 0;
    for (uint32_t i = 0; i < capture_count; i++) {
        uint32_t duration = pulse_duration(i);
        if (duration < 20 || duration > 10000) continue;
        if (duration <= split) {
            short_error += duration > short_us ? duration - short_us : short_us - duration;
            short_count++;
        } else {
            long_error += duration > long_us ? duration - long_us : long_us - duration;
            long_count++;
        }
    }
    uint32_t short_jitter = short_count && short_us ? (uint32_t)(short_error / short_count) * 100 / short_us : 100;
    uint32_t long_jitter = long_count && long_us ? (uint32_t)(long_error / long_count) * 100 / long_us : 100;
    uint32_t timing_confidence = 100 - MIN(100u, (short_jitter + long_jitter) / 2);
    uint32_t ratio_x100 = short_us ? long_us * 100 / short_us : 0;
    const char *encoding = "pulse-length";
    if (long_count && ratio_x100 >= 170 && ratio_x100 <= 240) encoding = "Manchester (probable)";
    else if (long_count && ratio_x100 >= 240 && ratio_x100 <= 450) encoding = "PWM/Princeton (probable)";
    else if (!long_count || maximum < minimum + minimum / 3) encoding = "Manchester (uniform timing probable)";

    uint32_t symbol_us = long_count ? short_us + long_us : short_us * 2;
    uint32_t bitrate = symbol_us ? 1000000u / symbol_us : 0;
    printf("Analysis: %s, short=%uus (%u%% jitter), long=%uus (%u%% jitter), bitrate about %ubps, timing confidence %u%%.\r\n",
           encoding, short_us, short_jitter, long_us, long_jitter, bitrate, timing_confidence);

    /* Decode the longest sequence of high/low PWM pairs as a Princeton payload. */
    uint64_t best_payload = 0;
    uint32_t best_bits = 0;
    for (uint32_t start = 0; start + 1 < capture_count; start++) {
        uint64_t payload = 0;
        uint32_t bits = 0;
        for (uint32_t i = start; i + 1 < capture_count && bits < 64; i += 2) {
            int32_t high = capture_buffer[i], low = capture_buffer[i + 1];
            if (high <= 0 || low >= 0) break;
            uint32_t h = (uint32_t)high, l = (uint32_t)-low;
            if (h > 10000 || l > 10000) break;
            bool h_short = h <= split, l_short = l <= split;
            if (h_short == l_short) break;
            payload = (payload << 1) | (h_short ? 0u : 1u);
            bits++;
        }
        if (bits > best_bits) { best_bits = bits; best_payload = payload; }
    }
    if (best_bits >= 4) {
        uint32_t possible_pairs = usable / 2;
        uint32_t confidence = possible_pairs ? MIN(100u, best_bits * 100 / possible_pairs) : 0;
        printf("PWM/Princeton candidate: 0x%08lx%08lx, %u bits, TE about %uus, confidence %u%%.\r\n",
               (unsigned long)(best_payload >> 32), (unsigned long)best_payload, best_bits, short_us, confidence);
    } else {
        printf("PWM/Princeton candidate: none.\r\n");
    }

    /* Expand runs into half-bit cells, then test both Manchester alignments. */
    uint32_t half_count = 0;
    for (uint32_t i = 0; i < capture_count && half_count < count_of(analyzer_half_bits); i++) {
        uint32_t duration = pulse_duration(i);
        if (duration < 20 || duration > short_us * 3) continue;
        uint32_t cells = (duration + short_us / 2) / short_us;
        if (!cells) cells = 1;
        if (cells > 2) cells = 2;
        uint8_t level = capture_buffer[i] > 0;
        while (cells-- && half_count < count_of(analyzer_half_bits)) analyzer_half_bits[half_count++] = level;
    }
    uint64_t manchester_payload = 0;
    uint32_t manchester_bits = 0;
    for (uint32_t offset = 0; offset < 2; offset++) {
        uint64_t candidate = 0;
        uint32_t consecutive = 0;
        for (uint32_t i = offset; i + 1 < half_count; i += 2) {
            if (analyzer_half_bits[i] == analyzer_half_bits[i + 1]) { candidate = 0; consecutive = 0; continue; }
            candidate = (candidate << 1) |
                        (analyzer_half_bits[i] == 0 && analyzer_half_bits[i + 1] == 1);
            if (++consecutive > 64) consecutive = 64;
            if (consecutive > manchester_bits) {
                manchester_bits = consecutive;
                manchester_payload = candidate;
            }
        }
    }
    if (manchester_bits >= 4) {
        uint32_t confidence = half_count ? MIN(100u, manchester_bits * 200 / half_count) : 0;
        printf("Manchester candidate: 0x%08lx%08lx, %u bits, bitrate about %ubps, confidence %u%%.\r\n",
               (unsigned long)(manchester_payload >> 32), (unsigned long)manchester_payload,
               manchester_bits, short_us ? 500000u / short_us : 0, confidence);
    } else {
        printf("Manchester candidate: none.\r\n");
    }
}

/* Latch a decoded frame for savekey / protocol replay. */
static void latch_decode(const subghz_protocol_entry_t *entry, subghz_protocol_decoder_t *dec) {
    if (last_decode.valid && !strcmp(last_decode.protocol, "Princeton") &&
        !strcmp(entry->info->name, "RcSwitch")) {
        return;
    }
    last_decode.valid = true;
    strncpy(last_decode.protocol, entry->info->name, sizeof(last_decode.protocol) - 1);
    last_decode.protocol[sizeof(last_decode.protocol) - 1] = '\0';
    last_decode.key = dec->decoded_data;
    last_decode.bits = dec->decoded_bits;
    last_decode.te_us = dec->te_us ? dec->te_us : entry->info->te_min_us;
    last_decode.frequency_khz = mode_config.frequency_khz;
    last_decode.preset = mode_config.preset;
    if (!strcmp(entry->info->name, "KeeLoq")) {
        strncpy(last_decode.manufacture, subghz_keeloq_last_manufacture(),
                sizeof(last_decode.manufacture) - 1);
    } else {
        last_decode.manufacture[0] = '\0';
    }
}

static void rx_print_callback(const subghz_protocol_entry_t *entry,
                              subghz_protocol_decoder_t *decoder, uint32_t repeats, void *ctx) {
    (void)ctx;
    (void)repeats;
    if (!strcmp(entry->info->name, "RcSwitch") && last_decode.valid &&
        !strcmp(last_decode.protocol, "Princeton") && last_decode.key == decoder->decoded_data) {
        return;
    }
    char detail[192];
    const char *text = subghz_decoder_get_string(decoder, detail, sizeof(detail));
    if (!text) {
        text = subghz_decoder_get_data(decoder, detail, sizeof(detail));
    }
    if (text) {
        printf("  [%s] %s\r\n", entry->info->name, text);
        latch_decode(entry, decoder);
    }
}

/* Run all registered protocol decoders against the capture buffer. */
static void run_protocol_decoders(void) {
    uint32_t count = subghz_protocol_count();
    if (count == 0) {
        return;
    }
    printf("Protocol decode attempt (%lu protocols):\r\n", (unsigned long)count);
    subghz_rx_t rx;
    if (!subghz_rx_init(&rx)) {
        printf("Decoder init failed.\r\n");
        return;
    }
    bool matched[SUBGHZ_RX_MAX_DECODERS] = { false };
    for (uint32_t i = 0; i < capture_count; i++) {
        int32_t val = capture_buffer[i];
        bool level = val > 0;
        uint32_t dur = (uint32_t)(val > 0 ? val : -val);
        for (uint32_t s = 0; s < rx.slot_count; s++) {
            subghz_protocol_status_t st =
                subghz_decoder_feed(rx.slots[s].decoder, level, dur);
            if (st == SUBGHZ_PROTOCOL_DONE) {
                char detail[192];
                const char *text = subghz_decoder_get_string(rx.slots[s].decoder, detail,
                                                             sizeof(detail));
                if (!text) {
                    text = subghz_decoder_get_data(rx.slots[s].decoder, detail, sizeof(detail));
                }
                if (text) {
                    if (!strcmp(rx.slots[s].entry->info->name, "RcSwitch") &&
                        last_decode.valid && !strcmp(last_decode.protocol, "Princeton") &&
                        last_decode.key == rx.slots[s].decoder->decoded_data) {
                        /* Suppress duplicate RcSwitch P1 print when canonical Princeton already matched */
                        matched[s] = true;
                    } else {
                        printf("  [%s] %s\r\n", rx.slots[s].entry->info->name, text);
                        latch_decode(rx.slots[s].entry, rx.slots[s].decoder);
                        matched[s] = true;
                    }
                }
                subghz_decoder_reset(rx.slots[s].decoder);
            }
        }
    }
    for (uint32_t s = 0; s < rx.slot_count; s++) {
        if (!matched[s]) {
            printf("  [%s] no match\r\n", rx.slots[s].entry->info->name);
        }
    }
    subghz_rx_free(&rx);
}

static void receive_handler(struct command_result *res) {
    if (bp_cmd_help_check(&receive_def, res->help_flag) || !command_ready()) return;
    uint32_t duration = 5000, value;
    if (bp_cmd_get_uint32(&receive_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) { printf("Capture time must be 1-60000ms.\r\n"); return; }
    printf("Raw GDO0 pulse capture at %u.%03uMHz [%s] (H/L:microseconds; press a key to stop)\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           cc1101_preset_name((cc1101_preset_t)mode_config.preset));
    capture_pulses(duration);
    if (bp_cmd_find_flag(&receive_def, 'v')) {
        printf("[raw %lu symbols | freq=%u.%03u MHz]\r\n", (unsigned long)capture_count,
               mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
        int col = 0;
        for (uint32_t i = 0; i < capture_count; i++) {
            int32_t duration_us = capture_buffer[i];
            bool lvl = duration_us > 0;
            uint32_t dur = (uint32_t)(lvl ? duration_us : -duration_us);
            printf("%c:%-5lu ", lvl ? 'H' : 'L', (unsigned long)dur);
            if (++col % 4 == 0) printf("\r\n");
            else printf("| ");
        }
        if (col % 4 != 0) printf("\r\n");
    } else {
        for (uint32_t i = 0; i < capture_count; i++) {
            int32_t duration_us = capture_buffer[i];
            printf("%c:%lu%s", duration_us > 0 ? 'H' : 'L',
                   (unsigned long)(duration_us > 0 ? duration_us : -duration_us),
                   ((i + 1) % 8) ? "  " : "\r\n");
        }
    }
    printf("\r\nCaptured %u pulses; ready for replay or record.\r\n", capture_count);
    analyze_capture();
    run_protocol_decoders();
}

static void sweep_handler(struct command_result *res) {
    if (bp_cmd_help_check(&sweep_def, res->help_flag) || !command_ready()) return;
    uint32_t band_low, band_high;
    frequency_band_bounds(mode_config.frequency_khz, &band_low, &band_high);
    uint32_t start = mode_config.frequency_khz > band_low + 1000 ? mode_config.frequency_khz - 1000 : band_low;
    uint32_t stop = mode_config.frequency_khz < band_high - 1000 ? mode_config.frequency_khz + 1000 : band_high;
    uint32_t step = 25, samples = 10, magnitude = 85, value;
    if (bp_cmd_get_uint32(&sweep_def, 'a', &value)) start = value;
    if (bp_cmd_get_uint32(&sweep_def, 'z', &value)) stop = value;
    if (bp_cmd_get_uint32(&sweep_def, 's', &value)) step = value;
    if (bp_cmd_get_uint32(&sweep_def, 'n', &value)) samples = value;
    if (bp_cmd_get_uint32(&sweep_def, 'r', &value)) magnitude = value;
    uint32_t points = step && stop >= start ? ((stop - start) / step) + 1 : 0;
    if (!step || samples < 2 || samples > 100 || magnitude > 127 ||
        !valid_frequency_range(start, stop) || points > 1000) {
        printf("Invalid sweep range/options.\r\n"); return;
    }
    printf("MHz       Peak  Active  Confidence\r\n");
    for (uint32_t khz = start; khz <= stop; khz += step) {
        if (!cc1101_set_frequency(khz) || !cc1101_rx()) {
            printf("Radio state transition failed during sweep.\r\n");
            break;
        }
        sleep_ms(2);
        int16_t peak = -127; uint32_t active = 0;
        for (uint32_t i = 0; i < samples; i++) {
            int16_t rssi = cc1101_rssi();
            if (rssi > peak) peak = rssi;
            if (rssi >= -(int16_t)magnitude) active++;
            sleep_ms(1);
        }
        uint32_t activity = active * 100 / samples;
        uint32_t strength = peak <= -110 ? 0 : peak >= -40 ? 100 : (uint32_t)(peak + 110) * 100 / 70;
        uint32_t confidence = (activity * 2 + strength) / 3;
        printf("%u.%03u  %4d  %3u%%    %3u%%\r\n", khz / 1000, khz % 1000, peak, activity, confidence);
        char c; if (rx_fifo_try_get(&c)) break;
        if (UINT32_MAX - khz < step) break;
    }
    if (!cc1101_set_frequency(mode_config.frequency_khz) || !cc1101_rx()) {
        printf("Warning: failed to restore the configured frequency.\r\n");
    }
}

static void trace_handler(struct command_result *res) {
    if (bp_cmd_help_check(&trace_def, res->help_flag) || !command_ready()) return;
    uint32_t duration = 2000, value;
    if (bp_cmd_get_uint32(&trace_def, 't', &value)) duration = value;
    if (duration < 100 || duration > 10000) { printf("Trace time must be 100-10000ms.\r\n"); return; }
    bio_input(SUBGHZ_GDO0);
    if (!cc1101_rx()) { printf("Failed to enter RX state.\r\n"); return; }
    printf("GDO0 trace (%ums, 100 columns):\r\n", duration);
    for (uint32_t column = 0; column < 100; column++) {
        bool high = false, low = false;
        absolute_time_t until = make_timeout_time_us((duration * 1000u) / 100u);
        while (!time_reached(until)) { if (bio_get(SUBGHZ_GDO0)) high = true; else low = true; }
        putchar(high && low ? '*' : high ? '-' : '_');
    }
    printf("\r\n              _=low  -=high  *=transition\r\n");
}

static char waterfall_pixel(int16_t rssi) {
    if (rssi >= -50) return '#';
    if (rssi >= -65) return 'O';
    if (rssi >= -80) return 'o';
    if (rssi >= -95) return '.';
    return ' ';
}

static void waterfall_handler(struct command_result *res) {
    if (bp_cmd_help_check(&waterfall_def, res->help_flag) || !command_ready()) return;
    uint32_t band_low, band_high;
    frequency_band_bounds(mode_config.frequency_khz, &band_low, &band_high);
    uint32_t start = mode_config.frequency_khz > band_low + 500 ? mode_config.frequency_khz - 500 : band_low;
    uint32_t stop = mode_config.frequency_khz < band_high - 500 ? mode_config.frequency_khz + 500 : band_high;
    uint32_t step = 25, rows = 20, value;
    if (bp_cmd_get_uint32(&waterfall_def, 'a', &value)) start = value;
    if (bp_cmd_get_uint32(&waterfall_def, 'z', &value)) stop = value;
    if (bp_cmd_get_uint32(&waterfall_def, 's', &value)) step = value;
    if (bp_cmd_get_uint32(&waterfall_def, 'n', &value)) rows = value;
    uint32_t columns = step && stop >= start ? ((stop - start) / step) + 1 : 0;
    if (!step || !valid_frequency_range(start, stop) || columns > 100 || !columns ||
        !rows || rows > 100) {
        printf("Invalid waterfall range (maximum 100 columns and 100 rows).\r\n"); return;
    }
    printf("Waterfall %u.%03u-%u.%03uMHz; # strongest, then O o .\r\n",
           start / 1000, start % 1000, stop / 1000, stop % 1000);
    bool failed = false;
    for (uint32_t row = 0; row < rows; row++) {
        printf("%03u|", row);
        for (uint32_t khz = start; khz <= stop; khz += step) {
            if (!cc1101_set_frequency(khz) || !cc1101_rx()) {
                failed = true;
                break;
            }
            sleep_ms(2);
            putchar(waterfall_pixel(cc1101_rssi()));
            if (UINT32_MAX - khz < step) break;
        }
        if (failed) printf("| radio state transition failed\r\n");
        else printf("|\r\n");
        if (failed) break;
        char c; if (rx_fifo_try_get(&c)) break;
    }
    if (!cc1101_set_frequency(mode_config.frequency_khz) || !cc1101_rx()) {
        printf("Warning: failed to restore the configured frequency.\r\n");
    }
}

static bool transmit_capture(uint32_t repeat) {
    if (!tx_guard()) return false;
    if (!cc1101_idle()) return false;
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
    apply_tx_power();
    if (!cc1101_tx()) {
        bio_input(SUBGHZ_GDO0);
        (void)cc1101_rx();
        return false;
    }
    sleep_ms(1);
    for (uint32_t r = 0; r < repeat; r++) {
        for (uint32_t i = 0; i < capture_count; i++) {
            int32_t duration = capture_buffer[i];
            pulse(duration > 0, (uint32_t)(duration > 0 ? duration : -duration));
        }
    }
    bio_put(SUBGHZ_GDO0, false);
    bool idle_ok = cc1101_idle();
    bio_input(SUBGHZ_GDO0);
    return idle_ok && cc1101_rx();
}

static void replay_handler(struct command_result *res) {
    if (bp_cmd_help_check(&replay_def, res->help_flag) || !command_ready()) return;
    uint32_t repeat = 1, value;
    if (bp_cmd_get_uint32(&replay_def, 'r', &value)) repeat = value;
    if (!capture_count) { printf("No capture in RAM. Run receive or record first.\r\n"); return; }
    if (!repeat || repeat > 20) { printf("Repeat must be 1-20.\r\n"); return; }
    printf("Replaying %u pulses %u time(s) at %u.%03uMHz.\r\n", capture_count, repeat,
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
    if (!transmit_capture(repeat)) printf("Replay failed: CC1101 state transition error.\r\n");
}

/* Streaming RAW writer: flat pulse stream, max 128 values per RAW_Data line. */
static bool record_write_pulse(FIL *file, int32_t pulse, uint32_t *column) {
    if ((*column % 128u) == 0 && f_puts("RAW_Data: ", file) < 0) {
        return false;
    }
    (*column)++;
    if (f_printf(file, "%ld%s", (long)pulse, (*column % 128u) ? " " : "\n") < 0) {
        return false;
    }
    return true;
}

static void record_handler(struct command_result *res) {
    if (bp_cmd_help_check(&record_def, res->help_flag) || !command_ready()) return;
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&record_def, 1, name, sizeof(name))) { bp_cmd_help_show(&record_def); return; }
    uint32_t duration = 5000, value;
    if (bp_cmd_get_uint32(&record_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) { printf("Capture time must be 1-60000ms.\r\n"); return; }

    const cc1101_preset_info_t *pi = cc1101_preset_info((cc1101_preset_t)mode_config.preset);
    (void)pi;
    const char *preset_name = preset_to_flipper_name((cc1101_preset_t)mode_config.preset);

    FIL file;
    FRESULT fr = f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { printf("Unable to create %s (FatFs error %u).\r\n", name, fr); return; }
    bool write_ok = f_printf(&file, "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu000\nPreset: %s\n",
                             (unsigned long)mode_config.frequency_khz, preset_name) >= 0;
    if (write_ok && mode_config.preset == CC1101_PRESET_CUSTOM) {
        uint8_t pairs[CC1101_CUSTOM_MAX_PAIRS * 2];
        size_t npairs = cc1101_custom_preset_get(pairs, CC1101_CUSTOM_MAX_PAIRS);
        uint8_t pa[CC1101_CUSTOM_PA_SIZE];
        bool have_pa = cc1101_custom_preset_get_pa(pa);
        if (!npairs) {
            write_ok = false;
        } else {
            write_ok = f_printf(&file, "Custom_preset_module: CC1101\nCustom_preset_data:") >= 0;
            for (size_t i = 0; i < npairs && write_ok; i++) {
                if (f_printf(&file, " %02X %02X", pairs[i * 2u], pairs[i * 2u + 1u]) < 0) {
                    write_ok = false;
                }
            }
            if (write_ok && have_pa) {
                if (f_printf(&file, " 00 00") < 0) {
                    write_ok = false;
                }
                for (size_t i = 0; i < sizeof(pa) && write_ok; i++) {
                    if (f_printf(&file, " %02X", pa[i]) < 0) {
                        write_ok = false;
                    }
                }
            }
            if (write_ok && f_printf(&file, "\n") < 0) {
                write_ok = false;
            }
        }
    }
    if (write_ok && f_printf(&file, "Protocol: RAW\n") < 0) {
        write_ok = false;
    }

    printf("Streaming capture to %s... press a key to stop.\r\n", name);
    if (!cc1101_rx()) {
        printf("Failed to enter RX state.\r\n");
        f_close(&file);
        return;
    }
    subghz_cap_start(SUBGHZ_GDO0);
    capture_count = 0;
    uint32_t total = 0, column = 0;
    bool started = false;
    absolute_time_t until = make_timeout_time_ms(duration);
    while (!time_reached(until) && total < 1000000u) {
        bool level;
        uint32_t dur;
        while (subghz_cap_poll(&level, &dur) && total < 1000000u) {
            if (!dur) {
                continue;
            }
            if (!started) {
                if (!level) {
                    continue;
                }
                started = true;
            }
            int32_t pulse = level ? (int32_t)dur : -(int32_t)dur;
            if (write_ok && !record_write_pulse(&file, pulse, &column)) {
                write_ok = false;
            }
            /* Retain the tail of the stream for replay/analysis. */
            if (capture_count < count_of(capture_buffer)) {
                capture_buffer[capture_count++] = pulse;
            } else {
                memmove(capture_buffer, capture_buffer + 256,
                        sizeof(capture_buffer[0]) * (count_of(capture_buffer) - 256));
                capture_count = count_of(capture_buffer) - 256;
                capture_buffer[capture_count++] = pulse;
            }
            total++;
        }
        char c;
        if (rx_fifo_try_get(&c)) break;
        tight_loop_contents();
    }
    subghz_cap_stop();
    if (write_ok && (column % 128u) && f_puts("\n", &file) < 0) {
        write_ok = false;
    }
    fr = f_close(&file);
    if (fr == FR_OK && write_ok && total) {
        printf("Saved %lu pulses to %s (tail %u in RAM).\r\n", (unsigned long)total, name,
               capture_count);
    } else if (!total) {
        printf("No pulses captured.\r\n");
        return;
    } else {
        printf("Storage write failed for %s.\r\n", name);
        return;
    }
    analyze_capture();
    run_protocol_decoders();
}

static void load_handler(struct command_result *res) {
    if (bp_cmd_help_check(&load_def, res->help_flag) || !command_ready()) return;
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&load_def, 1, name, sizeof(name))) {
        if (!ui_file_pick("sub", name, sizeof(name), NULL)) {
            printf("No .sub file selected.\r\n");
            return;
        }
    }
    uint32_t repeat = 0, value;
    if (bp_cmd_get_uint32(&load_def, 'r', &value)) repeat = value;
    if (repeat > 20) { printf("Repeat must be 0-20.\r\n"); return; }
    load_file(name, repeat);
}

static void load_file(const char *name, uint32_t repeat) {
    FIL file;
    FRESULT fr = f_open(&file, name, FA_READ);
    if (fr != FR_OK) { printf("Unable to open %s (FatFs error %u).\r\n", name, fr); return; }

    char line[256];
    uint32_t file_frequency_khz = 0;
    bool valid_filetype = false, valid_version = false;
    bool raw_continuation = false, raw_protocol = false, binraw_protocol = false;
    bool preset_found = false, truncated = false, raw_valid = true, raw_have_pulse = false;
    int raw_last_sign = 0;
    cc1101_preset_t file_preset = CC1101_PRESET_OOK_270;
    char proto_name[SUBGHZ_PROTOCOL_NAME_MAX] = { 0 };
    uint64_t proto_key = 0;
    uint32_t proto_bits = 0, proto_te = 0;
    bool have_proto = false, have_key = false, have_bits = false, have_te = false;
    bool have_duration_counter = false;
    uint32_t duration_counter = 0;
    bool have_custom = false;
    char file_manufacture[32] = { 0 };
    uint8_t custom_file_pairs[CC1101_CUSTOM_MAX_PAIRS * 2];
    size_t custom_file_nPairs = 0;
    uint8_t custom_file_pa[CC1101_CUSTOM_PA_SIZE];
    bool custom_file_have_pa = false;
    /* BinRAW accumulation (Flipper: Bit/TE + repeated Bit_RAW/Data_RAW). */
    uint32_t binraw_total_bits = 0, binraw_sum_bits = 0, binraw_te = 0, binraw_blocks = 0;
    uint32_t binraw_pending_bits = 0;
    bool binraw_have_total = false, binraw_have_te = false, binraw_have_pending = false;
    capture_count = 0;
    while (f_gets(line, sizeof(line), &file)) {
        bool complete_line = strchr(line, '\n') != NULL;
        char *data = NULL;
        if (!strncmp(line, "Filetype:", 9)) {
            valid_filetype = field_value_equals(line, 9, "Flipper SubGhz RAW File") ||
                             field_value_equals(line, 9, "Flipper SubGhz Key File");
        } else if (!strncmp(line, "Version:", 8)) {
            valid_version = field_value_equals(line, 8, "1");
        } else if (!strncmp(line, "Frequency:", 10)) {
            if (!parse_frequency_field(line, &file_frequency_khz)) file_frequency_khz = 0;
        } else if (!strncmp(line, "Preset:", 7)) {
            preset_found = preset_from_flipper_name(line, 7, &file_preset);
        } else if (!strncmp(line, "Protocol:", 9)) {
            raw_protocol = field_value_equals(line, 9, "RAW");
            binraw_protocol = field_value_equals(line, 9, "BinRAW");
            if (!raw_protocol && !binraw_protocol) {
                const char *v = line + 9;
                while (*v == ' ' || *v == '\t') v++;
                size_t n = strcspn(v, "\r\n");
                if (n > 0 && n < sizeof(proto_name)) {
                    memcpy(proto_name, v, n);
                    proto_name[n] = '\0';
                    have_proto = true;
                }
            }
        } else if (!strncmp(line, "Bit:", 4)) {
            if (binraw_protocol) {
                /* BinRAW total payload length: up to 4096 bits. */
                const char *colon = strchr(line, ':');
                char *end = NULL;
                errno = 0;
                unsigned long v = colon ? strtoul(colon + 1, &end, 10) : 0;
                if (!colon || errno == ERANGE || end == colon + 1 || !v ||
                    v > SUBGHZ_BINRAW_MAX_BITS) {
                    binraw_have_total = false;
                } else {
                    while (*end == ' ' || *end == '\t') end++;
                    if (*end == '\r' || *end == '\n' || *end == '\0') {
                        binraw_total_bits = (uint32_t)v;
                        binraw_have_total = true;
                    } else {
                        binraw_have_total = false;
                    }
                }
            } else {
                have_bits = parse_bit_field(line, &proto_bits);
            }
        } else if (!strncmp(line, "Bit_RAW:", 8)) {
            binraw_have_pending =
                subghz_parse_bit_raw_field(line, &binraw_pending_bits);
            if (!binraw_have_pending || !binraw_protocol) {
                binraw_have_pending = false;
                raw_valid = false;
            }
        } else if (!strncmp(line, "Data_RAW:", 9)) {
            if (!binraw_protocol || !binraw_have_pending) {
                raw_valid = false;
            } else {
                uint8_t bytes[SUBGHZ_BINRAW_MAX_BYTES];
                size_t nbytes = 0;
                uint32_t want = binraw_pending_bits;
                uint32_t te = binraw_have_te ? binraw_te : proto_te;
                if (!subghz_parse_data_raw_field(line, bytes, &nbytes, sizeof(bytes)) ||
                    nbytes * 8u < want) {
                    raw_valid = false;
                } else if ((!binraw_have_te && !have_te) || !te) {
                    /* Flipper order is TE before the blocks; out-of-order
                     * files are rejected rather than mis-expanded. */
                    raw_valid = false;
                } else {
                    uint32_t before = capture_count;
                    uint32_t next = capture_count;
                    /* Expand into a scratch tail so a full buffer truncates
                     * instead of leaving a half-written run. */
                    if (!subghz_binraw_expand(te, bytes, want, capture_buffer, &next,
                                              count_of(capture_buffer))) {
                        truncated = true;
                    }
                    if (next > before) {
                        capture_count = next;
                        raw_have_pulse = true;
                        raw_last_sign =
                            capture_buffer[capture_count - 1] > 0 ? 1 : -1;
                        binraw_sum_bits += want;
                        binraw_blocks++;
                    } else {
                        truncated = true;
                    }
                    binraw_have_pending = false;
                }
            }
        } else if (!strncmp(line, "Key:", 4)) {
            uint32_t keybits = 0;
            have_key = parse_key_field(line, &proto_key, &keybits);
        } else if (!strncmp(line, "Duration_Counter:", 17)) {
            const char *colon = strchr(line, ':');
            char *end = NULL;
            errno = 0;
            unsigned long v = colon ? strtoul(colon + 1, &end, 10) : 0;
            if (colon && !errno && end != colon + 1 && v <= 0xfffffffful) {
                while (*end == ' ' || *end == '\t') {
                    end++;
                }
                if (*end == '\r' || *end == '\n' || *end == '\0') {
                    duration_counter = (uint32_t)v;
                    have_duration_counter = true;
                }
            }
        } else if (!strncmp(line, "TE:", 3)) {
            have_te = parse_te_field(line, &proto_te);
            if (have_te) {
                binraw_te = proto_te;
                binraw_have_te = true;
            }
        } else if (!strncmp(line, "Manufacture:", 12)) {
            const char *v = line + 12;
            while (*v == ' ' || *v == '\t') v++;
            size_t n = strcspn(v, "\r\n");
            if (n > 0 && n < sizeof(file_manufacture)) {
                memcpy(file_manufacture, v, n);
                file_manufacture[n] = '\0';
            }
        } else if (!strncmp(line, "Custom_preset_data:", 19)) {
            uint8_t pa[CC1101_CUSTOM_PA_SIZE];
            bool have_pa = false;
            if (parse_custom_preset_full(line, custom_file_pairs, &custom_file_nPairs,
                                         pa, &have_pa)) {
                have_custom = true;
                custom_file_have_pa = have_pa;
                if (have_pa) {
                    memcpy(custom_file_pa, pa, sizeof(custom_file_pa));
                }
            } else {
                have_custom = false;
                printf("Invalid Custom_preset_data in %s.\r\n", name);
                f_close(&file);
                capture_count = 0;
                return;
            }
        } else if (!strncmp(line, "RAW_Data:", 9)) {
            data = line + 9;
            raw_continuation = !complete_line;
        } else if (raw_continuation) {
            data = line;
            raw_continuation = !complete_line;
        }

        if (data) {
            if (binraw_protocol) {
                /* BinRAW files must not interleave RAW_Data lines. */
                raw_valid = false;
            } else {
                char *cursor = data;
                while (*cursor) {
                    while (*cursor && *cursor != '-' && (*cursor < '0' || *cursor > '9')) {
                        cursor++;
                    }
                    if (!*cursor) {
                        break;
                    }
                    char *end = cursor;
                    long duration = strtol(cursor, &end, 10);
                    if (end == cursor) {
                        cursor++;
                        continue;
                    }
                    if (!duration || duration < -60000000l || duration > 60000000l) {
                        raw_valid = false;
                    } else {
                        int sign = duration > 0 ? 1 : -1;
                        if (!raw_have_pulse) {
                            /* Flipper: RAW streams start with a positive pulse
                             * and strictly alternate sign. */
                            if (sign < 0) {
                                raw_valid = false;
                            } else {
                                raw_have_pulse = true;
                                raw_last_sign = sign;
                            }
                        } else if (sign == raw_last_sign) {
                            raw_valid = false;
                        } else {
                            raw_last_sign = sign;
                        }
                        if (raw_valid) {
                            if (capture_count < count_of(capture_buffer)) {
                                capture_buffer[capture_count++] = (int32_t)duration;
                            } else {
                                truncated = true;
                            }
                        }
                    }
                    cursor = end;
                }
            }
        }
    }
    fr = f_close(&file);
    if (fr != FR_OK) { capture_count = 0; printf("Error closing %s.\r\n", name); return; }
    if (have_custom) {
        const uint8_t *pa = custom_file_have_pa ? custom_file_pa : NULL;
        if (!cc1101_custom_preset_set_full(custom_file_pairs, custom_file_nPairs, pa)) {
            printf("Invalid Custom_preset_data in %s.\r\n", name);
            capture_count = 0;
            return;
        }
        file_preset = CC1101_PRESET_CUSTOM;
        preset_found = true;
        printf("Custom preset loaded (%u registers%s).\r\n", (unsigned)custom_file_nPairs,
               custom_file_have_pa ? ", PA table" : "");
    }
    if (!valid_filetype || !valid_version || !valid_frequency(file_frequency_khz) ||
        !preset_found) {
        capture_count = 0;
        printf("Unsupported .sub file (need Filetype/Version/Frequency/Preset).\r\n");
        return;
    }
    if (binraw_have_pending) {
        capture_count = 0;
        printf("Unsupported .sub file (Bit_RAW without Data_RAW).\r\n");
        return;
    }

    /* BinRAW file path: TE + Bit_RAW/Data_RAW blocks expanded above. */
    if (binraw_protocol) {
        if (!binraw_blocks || !capture_count || !raw_valid ||
            (!binraw_have_te && !have_te)) {
            capture_count = 0;
            printf("Unsupported .sub file (BinRAW needs TE and Bit_RAW/Data_RAW).\r\n");
            return;
        }
        if (binraw_have_total && binraw_sum_bits != binraw_total_bits) {
            capture_count = 0;
            printf("Unsupported .sub file (BinRAW bit count mismatch).\r\n");
            return;
        }
        uint32_t previous_frequency_khz = mode_config.frequency_khz;
        uint32_t previous_preset = mode_config.preset;
        if (!cc1101_set_preset(file_preset) || !cc1101_set_frequency(file_frequency_khz)) {
            (void)cc1101_set_preset((cc1101_preset_t)previous_preset);
            (void)cc1101_set_frequency(previous_frequency_khz);
            (void)cc1101_rx();
            capture_count = 0;
            printf("Failed to apply the .sub radio preset.\r\n");
            return;
        }
        mode_config.frequency_khz = file_frequency_khz;
        mode_config.preset = file_preset;
        if (!cc1101_rx()) {
            mode_config.frequency_khz = previous_frequency_khz;
            mode_config.preset = previous_preset;
            (void)cc1101_set_preset((cc1101_preset_t)previous_preset);
            (void)cc1101_set_frequency(previous_frequency_khz);
            (void)cc1101_rx();
            capture_count = 0;
            printf("Failed to enter RX state.\r\n");
            return;
        }
        printf("Loaded BinRAW %lu bits (%u pulses) from %s at %u.%03uMHz, preset=%s%s.\r\n",
               (unsigned long)binraw_sum_bits, capture_count, name,
               mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
               cc1101_preset_name((cc1101_preset_t)mode_config.preset),
               truncated ? " (truncated to buffer capacity)" : "");
        analyze_capture();
        run_protocol_decoders();
        if (repeat) {
            printf("Replaying loaded capture %u time(s).\r\n", repeat);
            if (!transmit_capture(repeat)) {
                printf("Replay failed: CC1101 state transition error.\r\n");
            }
        }
        return;
    }

    /* Protocol key file path (Flipper SubGhz Key File). */
    if (!raw_protocol && have_proto) {
        const subghz_protocol_entry_t *entry = subghz_protocol_find(proto_name);
        if (!entry) {
            printf("Protocol '%s' is not supported by this firmware.\r\n", proto_name);
            return;
        }
        bool is_keytis80 =
            !strcmp(entry->info->name, "Somfy Keytis") && have_bits && proto_bits == 80u;
        if (!have_key || !have_bits || !proto_bits ||
            (proto_bits > 64u && !is_keytis80)) {
            printf("Protocol file is missing a valid Bit/Key pair.\r\n");
            return;
        }
        /* Somfy Keytis 80-bit files: 8-byte Key (top byte zero, low 56 are
         * the data core) plus Duration_Counter (press counter, low 24). */
        uint32_t keytis_pdc = 0;
        if (is_keytis80) {
            if ((proto_key >> 56) != 0 || !have_duration_counter) {
                printf("Protocol file is missing a valid Bit/Key pair.\r\n");
                return;
            }
            keytis_pdc = duration_counter & 0xffffffu;
            proto_key &= 0x00ffffffffffffffull;
            proto_bits = 56u;
        }
        if (!cc1101_preset_is_ook(file_preset)) {
            printf("Protocol files require an OOK preset for async TX.\r\n");
            return;
        }
        uint32_t previous_frequency_khz = mode_config.frequency_khz;
        uint32_t previous_preset = mode_config.preset;
        if (!cc1101_set_preset(file_preset) || !cc1101_set_frequency(file_frequency_khz) ||
            !cc1101_rx()) {
            (void)cc1101_set_preset((cc1101_preset_t)previous_preset);
            (void)cc1101_set_frequency(previous_frequency_khz);
            (void)cc1101_rx();
            printf("Failed to apply the .sub radio preset.\r\n");
            return;
        }
        mode_config.frequency_khz = file_frequency_khz;
        mode_config.preset = file_preset;
        last_decode.valid = true;
        strncpy(last_decode.protocol, entry->info->name, sizeof(last_decode.protocol) - 1);
        last_decode.key = proto_key;
        last_decode.bits = proto_bits;
        last_decode.te_us = have_te ? proto_te : entry->info->te_min_us;
        last_decode.frequency_khz = file_frequency_khz;
        last_decode.preset = file_preset;
        strncpy(last_decode.manufacture, file_manufacture, sizeof(last_decode.manufacture) - 1);
        printf("Loaded %s %ubit key 0x%08lx%08lx from %s at %u.%03uMHz.\r\n", proto_name,
               proto_bits, (unsigned long)(proto_key >> 32), (unsigned long)proto_key, name,
               file_frequency_khz / 1000, file_frequency_khz % 1000);
        if (file_manufacture[0]) {
            printf("Manufacture: %s\r\n", file_manufacture);
        }
        if (is_keytis80) {
            printf("Duration_Counter: 0x%06lX\r\n", (unsigned long)keytis_pdc);
        }
        char detail[192];
        subghz_protocol_decoder_t *dec = subghz_decoder_alloc(entry);
        if (dec) {
            /* Feed the key through the encoder and back to validate the round trip. */
            subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
            if (enc && subghz_encoder_feed(enc, proto_key, proto_bits) ==
                            SUBGHZ_PROTOCOL_DONE) {
                subghz_decoder_reset(dec);
                bool level;
                uint32_t dur;
                bool ok = false;
                for (int pass = 0; pass < 2 && !ok; pass++) {
                    subghz_encoder_reset(enc);
                    while (subghz_encoder_yield(enc, &level, &dur)) {
                        if (subghz_decoder_feed(dec, level, dur) == SUBGHZ_PROTOCOL_DONE) {
                            ok = true;
                            break;
                        }
                    }
                }
                if (ok) {
                    const char *text =
                        subghz_decoder_get_string(dec, detail, sizeof(detail));
                    if (text) {
                        printf("  [%s] %s\r\n", proto_name, text);
                    }
                }
            }
            if (enc) {
                subghz_encoder_free(enc);
            }
            subghz_decoder_free(dec);
        }
        if (entry->info->rolling || !entry->encoder_ops) {
            printf("Note: '%s' is decode-only; replay refused (rolling code).\r\n", proto_name);
            return;
        }
        if (repeat) {
            subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
            if (enc && subghz_encoder_feed(enc, proto_key, proto_bits) ==
                            SUBGHZ_PROTOCOL_DONE) {
                printf("Replaying protocol key %u time(s).\r\n", repeat);
                if (!transmit_upload(enc, repeat)) {
                    printf("Replay failed: CC1101 state transition error.\r\n");
                }
            } else {
                printf("Encoder rejected the protocol key.\r\n");
            }
            if (enc) {
                subghz_encoder_free(enc);
            }
        }
        return;
    }

    if (!raw_protocol || !capture_count || !raw_valid) {
        capture_count = 0;
        printf("Unsupported .sub file. A valid RAW file with a supported frequency is required.\r\n");
        return;
    }

    uint32_t previous_frequency_khz = mode_config.frequency_khz;
    uint32_t previous_preset = mode_config.preset;
    if (!cc1101_set_preset(file_preset) || !cc1101_set_frequency(file_frequency_khz)) {
        (void)cc1101_set_preset((cc1101_preset_t)previous_preset);
        (void)cc1101_set_frequency(previous_frequency_khz);
        (void)cc1101_rx();
        capture_count = 0;
        printf("Failed to apply the .sub radio preset.\r\n");
        return;
    }
    mode_config.frequency_khz = file_frequency_khz;
    mode_config.preset = file_preset;
    if (!cc1101_rx()) {
        mode_config.frequency_khz = previous_frequency_khz;
        mode_config.preset = previous_preset;
        (void)cc1101_set_preset((cc1101_preset_t)previous_preset);
        (void)cc1101_set_frequency(previous_frequency_khz);
        (void)cc1101_rx();
        capture_count = 0;
        printf("Failed to enter RX state.\r\n");
        return;
    }
    printf("Loaded %u pulses from %s at %u.%03uMHz, preset=%s%s.\r\n", capture_count, name,
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           cc1101_preset_name((cc1101_preset_t)mode_config.preset),
           truncated ? " (truncated to buffer capacity)" : "");
    analyze_capture();
    run_protocol_decoders();
    if (repeat) {
        printf("Replaying loaded capture %u time(s).\r\n", repeat);
        if (!transmit_capture(repeat)) printf("Replay failed: CC1101 state transition error.\r\n");
    }
}

static void decode_handler(struct command_result *res) {
    if (bp_cmd_help_check(&decode_def, res->help_flag) || !command_ready()) return;
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&decode_def, 1, name, sizeof(name))) {
        if (!ui_file_pick("sub", name, sizeof(name), NULL)) {
            printf("No .sub file selected.\r\n");
            return;
        }
    }
    printf("Decoding %s (radio will not transmit).\r\n", name);
    load_file(name, 0);
}

static bool savekey_file(const char *name) {
    const subghz_protocol_entry_t *entry = subghz_protocol_find(last_decode.protocol);
    if (!entry) {
        printf("Last protocol '%s' is no longer registered.\r\n", last_decode.protocol);
        return false;
    }
    const cc1101_preset_info_t *pi = cc1101_preset_info((cc1101_preset_t)last_decode.preset);
    (void)pi;
    const char *preset_name = preset_to_flipper_name((cc1101_preset_t)last_decode.preset);
    FIL file;
    FRESULT fr = f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("Unable to create %s (FatFs error %u).\r\n", name, fr);
        return false;
    }
    uint64_t key = last_decode.key;
    uint8_t bytes[8];
    for (int i = 7; i >= 0; i--) {
        bytes[i] = (uint8_t)(key & 0xffu);
        key >>= 8;
    }
    uint32_t nbytes = (last_decode.bits + 7u) / 8u;
    if (!nbytes || nbytes > 8u) {
        nbytes = 8u;
    }
    bool ok = f_printf(&file,
                       "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: %lu000\n"
                       "Preset: %s\n",
                       (unsigned long)last_decode.frequency_khz, preset_name) >= 0;
    if (ok && last_decode.preset == CC1101_PRESET_CUSTOM) {
        uint8_t pairs[CC1101_CUSTOM_MAX_PAIRS * 2];
        size_t npairs = cc1101_custom_preset_get(pairs, CC1101_CUSTOM_MAX_PAIRS);
        uint8_t pa[CC1101_CUSTOM_PA_SIZE];
        bool have_pa = cc1101_custom_preset_get_pa(pa);
        if (!npairs) {
            ok = false;
        } else {
            ok = f_printf(&file, "Custom_preset_module: CC1101\nCustom_preset_data:") >= 0;
            for (size_t i = 0; i < npairs && ok; i++) {
                if (f_printf(&file, " %02X %02X", pairs[i * 2u], pairs[i * 2u + 1u]) < 0) {
                    ok = false;
                }
            }
            if (ok && have_pa) {
                if (f_printf(&file, " 00 00") < 0) {
                    ok = false;
                }
                for (size_t i = 0; i < sizeof(pa) && ok; i++) {
                    if (f_printf(&file, " %02X", pa[i]) < 0) {
                        ok = false;
                    }
                }
            }
            if (ok && f_printf(&file, "\n") < 0) {
                ok = false;
            }
        }
    }
    if (ok && f_printf(&file, "Protocol: %s\nBit: %lu\nKey:", last_decode.protocol,
                       (unsigned long)last_decode.bits) < 0) {
        ok = false;
    }
    for (uint32_t i = 8u - nbytes; i < 8u && ok; i++) {
        if (f_printf(&file, "%s%02X", i == 8u - nbytes ? " " : " ", bytes[i]) < 0) {
            ok = false;
        }
    }
    if (ok && f_printf(&file, "\nTE: %lu\n", (unsigned long)last_decode.te_us) < 0) {
        ok = false;
    }
    if (ok && last_decode.manufacture[0] &&
        f_printf(&file, "Manufacture: %s\n", last_decode.manufacture) < 0) {
        ok = false;
    }
    fr = f_close(&file);
    if (fr == FR_OK && ok) {
        printf("Saved %s %ubit key to %s.\r\n", last_decode.protocol, last_decode.bits, name);
        return true;
    }
    printf("Storage write failed for %s.\r\n", name);
    return false;
}

static void savekey_handler(struct command_result *res) {
    if (bp_cmd_help_check(&savekey_def, res->help_flag) || !command_ready()) return;
    if (!last_decode.valid) {
        printf("No decoded frame yet. Run receive, load, decode, or monitor first.\r\n");
        return;
    }
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&savekey_def, 1, name, sizeof(name))) {
        bp_cmd_help_show(&savekey_def);
        return;
    }
    savekey_file(name);
}

static void clone_handler(struct command_result *res) {
    if (bp_cmd_help_check(&clone_def, res->help_flag) || !command_ready()) return;
    uint32_t duration = 5000, value;
    if (bp_cmd_get_uint32(&clone_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) {
        printf("Capture time must be 1-60000ms.\r\n");
        return;
    }
    char name[BP_FILENAME_MAX] = { 0 };
    bool have_name = bp_cmd_get_positional_string(&clone_def, 1, name, sizeof(name));
    last_decode.valid = false;
    printf("Cloning at %u.%03uMHz: press the remote now (capturing %lums)...\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           (unsigned long)duration);
    if (!capture_pulses(duration)) {
        printf("No pulses captured.\r\n");
        return;
    }
    analyze_capture();
    run_protocol_decoders();
    if (!last_decode.valid) {
        printf("No protocol matched. Use 'record capture.sub' for a RAW copy.\r\n");
        return;
    }
    const subghz_protocol_entry_t *entry = subghz_protocol_find(last_decode.protocol);
    if (!have_name) {
        /* Auto-name: Protocol_Key.sub with spaces sanitized. */
        size_t pos = 0;
        for (const char *p = last_decode.protocol; *p && pos + 1 < sizeof(name); p++) {
            name[pos++] = (*p == ' ') ? '_' : *p;
        }
        if (pos + 1 < sizeof(name)) {
            name[pos++] = '_';
        }
        snprintf(name + pos, sizeof(name) - pos, "%llX.sub",
                 (unsigned long long)(last_decode.key & 0xffffffffull));
    }
    if (!savekey_file(name)) {
        return;
    }
    if (entry && (entry->info->rolling || !entry->encoder_ops)) {
        printf("Rolling code saved for analysis; replay stays disabled.\r\n");
    } else {
        printf("Replay with: load %s -r 10\r\n", name);
    }
}

static void monitor_handler(struct command_result *res) {
    if (bp_cmd_help_check(&monitor_def, res->help_flag) || !command_ready()) return;
    uint32_t duration = 10000, value;
    if (bp_cmd_get_uint32(&monitor_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) {
        printf("Monitor time must be 1-60000ms.\r\n");
        return;
    }
    subghz_rx_t rx;
    if (!subghz_rx_init(&rx)) {
        printf("Decoder init failed.\r\n");
        return;
    }
    bio_input(SUBGHZ_GDO0);
    if (!cc1101_rx()) {
        printf("Failed to enter RX state.\r\n");
        subghz_rx_free(&rx);
        return;
    }
    printf("Live decode at %u.%03uMHz for %lums (press a key to stop):\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           (unsigned long)duration);
    subghz_cap_start(SUBGHZ_GDO0);
    absolute_time_t until = make_timeout_time_ms(duration);
    while (!time_reached(until)) {
        bool level;
        uint32_t dur;
        while (subghz_cap_poll(&level, &dur)) {
            if (dur && dur < 60000000u) {
                subghz_rx_feed(&rx, level, dur, rx_print_callback, NULL);
            }
        }
        char c;
        if (rx_fifo_try_get(&c)) {
            break;
        }
        tight_loop_contents();
    }
    subghz_cap_stop();
    if (subghz_cap_overruns()) {
        printf("Warning: capture overrun during monitor.\r\n");
    }
    subghz_rx_free(&rx);
    printf("Monitor done.\r\n");
}

static void keyload_handler(struct command_result *res) {
    if (bp_cmd_help_check(&keyload_def, res->help_flag)) return;
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&keyload_def, 1, name, sizeof(name))) {
        strncpy(name, SUBGHZ_KEY_FILE, sizeof(name) - 1);
    }
    FIL file;
    FRESULT fr = f_open(&file, name, FA_READ);
    if (fr != FR_OK) {
        printf("Unable to open %s (FatFs error %u).\r\n", name, fr);
        return;
    }
    subghz_keystore_clear(&keystore);
    char line[96];
    uint32_t added = 0, rejected = 0;
    while (f_gets(line, sizeof(line), &file)) {
        if (subghz_keystore_parse_line(&keystore, line)) {
            added++;
        } else {
            rejected++;
        }
    }
    f_close(&file);
    keystore_loaded = true;
    subghz_keeloq_keystore_set(&keystore);
    printf("Keystore: %lu keys from %s%s.\r\n", (unsigned long)added, name,
           rejected ? " (some lines skipped)" : "");
    printf("Types 0-7 supported (simple/normal/secure/magic); TX stays disabled.\r\n");
}

static void keys_handler(struct command_result *res) {
    if (bp_cmd_help_check(&keys_def, res->help_flag)) return;
    if (!keystore.count) {
        printf("Keystore empty. Use 'keyload <file>' or 'keyadd <name> <hexkey>'.\r\n");
        return;
    }
    printf("Manufacturer keys (%lu):\r\n", (unsigned long)keystore.count);
    char line[96];
    for (uint32_t i = 0; i < keystore.count; i++) {
        if (subghz_keystore_format_line(&keystore, i, line, sizeof(line))) {
            printf("  %s\r\n", line);
        }
    }
}

static void keyadd_handler(struct command_result *res) {
    if (bp_cmd_help_check(&keyadd_def, res->help_flag)) return;
    char name[SUBGHZ_PROTOCOL_NAME_MAX] = { 0 };
    char keytext[32] = { 0 };
    char typetext[8] = { 0 };
    if (!bp_cmd_get_positional_string(&keyadd_def, 1, name, sizeof(name)) ||
        !bp_cmd_get_positional_string(&keyadd_def, 2, keytext, sizeof(keytext))) {
        bp_cmd_help_show(&keyadd_def);
        return;
    }
    char *end = NULL;
    errno = 0;
    uint64_t key = strtoull(keytext, &end, 16);
    if (errno == ERANGE || !end || end == keytext || *end != '\0') {
        printf("Key must be up to 16 hex digits.\r\n");
        return;
    }
    uint8_t type = SUBGHZ_KEY_TYPE_SIMPLE;
    if (bp_cmd_get_positional_string(&keyadd_def, 3, typetext, sizeof(typetext))) {
        type = (uint8_t)strtoul(typetext, NULL, 10);
        if (type > SUBGHZ_KEY_TYPE_MAGIC_S3) {
            printf("Type must be 0-7 (0 unknown, 1 simple, 2 normal, 3 secure, 4-7 magic).\r\n");
            return;
        }
    }
    if (!subghz_keystore_add(&keystore, name, key, type)) {
        printf("Keystore full (%u keys).\r\n", SUBGHZ_KEYSTORE_MAX_KEYS);
        return;
    }
    subghz_keeloq_keystore_set(&keystore);
    printf("Added key '%s' (type %u). Use 'keyload' file import to persist.\r\n", name, type);
}

static void pulse(bool level, uint32_t us) {
    bio_put(SUBGHZ_GDO0, level);
    if (us <= 10000u) busy_wait_us_32(us);
    else sleep_us(us);
}

static uint32_t subghz_rand(void) {
    static uint32_t state = 0x853c49e6;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void send_random_burst(void) {
    /* Mean 200us, +/-30% jitter -> 140us to 260us (from SubGhzService::sendRandomBurst) */
    int level = (int)(subghz_rand() & 1u);
    for (int i = 0; i < 256; i++) {
        uint32_t dur0 = 140u + (subghz_rand() % 121u);
        pulse(level, dur0);
        level ^= 1;
        uint32_t dur1 = 140u + (subghz_rand() % 121u);
        pulse(level, dur1);
        level ^= 1;
    }
    bio_put(SUBGHZ_GDO0, false);
}

static void send_raw_pulse(int32_t duration) {
    if (duration == 0) return;
    if (duration < 0) {
        pulse(false, (uint32_t)(-duration));
    } else {
        pulse(true, (uint32_t)duration);
    }
}

/* Transmit one encoder upload <repeat> times (Flipper TX worker equivalent). */
static bool transmit_upload(subghz_protocol_encoder_t *enc, uint32_t repeat) {
    if (!cc1101_idle()) {
        return false;
    }
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
    apply_tx_power();
    if (!cc1101_tx()) {
        bio_input(SUBGHZ_GDO0);
        (void)cc1101_rx();
        return false;
    }
    sleep_ms(1);
    for (uint32_t r = 0; r < repeat; r++) {
        subghz_encoder_reset(enc);
        bool level;
        uint32_t dur;
        while (subghz_encoder_yield(enc, &level, &dur)) {
            pulse(level, dur);
            char c;
            if (rx_fifo_try_get(&c)) {
                break;
            }
        }
        /* Inter-frame gap. */
        pulse(false, 10000u);
    }
    bio_put(SUBGHZ_GDO0, false);
    bool idle_ok = cc1101_idle();
    bio_input(SUBGHZ_GDO0);
    return idle_ok && cc1101_rx();
}

static void send_handler(struct command_result *res) {
    if (bp_cmd_help_check(&send_def, res->help_flag) || !command_ready()) return;
    if (!tx_guard()) return;

    if (bp_cmd_find_flag(&send_def, 'R')) {
        printf("Transmitting random noise burst at %u.%03u MHz...\r\n",
               mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
        if (!cc1101_idle()) {
            printf("CC1101 state transition failed.\r\n");
            return;
        }
        bio_output(SUBGHZ_GDO0);
        bio_put(SUBGHZ_GDO0, false);
        apply_tx_power();
        if (!cc1101_tx()) {
            bio_input(SUBGHZ_GDO0);
            (void)cc1101_rx();
            printf("CC1101 TX transition failed.\r\n");
            return;
        }
        send_random_burst();
        bio_put(SUBGHZ_GDO0, false);
        cc1101_idle();
        bio_input(SUBGHZ_GDO0);
        (void)cc1101_rx();
        printf("Burst sent.\r\n");
        return;
    }

    char protoname[32] = { 0 };
    char keytext[32] = { 0 };
    bool have_proto = bp_cmd_get_string(&send_def, 'P', protoname, sizeof(protoname));
    bool have_key = bp_cmd_get_string(&send_def, 'k', keytext, sizeof(keytext));
    uint32_t bits = 24, te = 350, repeat = 4, value;
    if (bp_cmd_get_uint32(&send_def, 'b', &value)) bits = value;
    if (bp_cmd_get_uint32(&send_def, 't', &value)) te = value;
    if (bp_cmd_get_uint32(&send_def, 'r', &value)) repeat = value;

    if (have_proto || have_key) {
        /* Protocol encoder path: send -P <protocol> -k <hexkey>. */
        if (!have_proto || !have_key) {
            printf("Protocol send needs both -P <protocol> and -k <hexkey>.\r\n");
            return;
        }
        const subghz_protocol_entry_t *entry = subghz_protocol_find(protoname);
        if (!entry) {
            printf("Unknown protocol '%s'.\r\n", protoname);
            return;
        }
        if (entry->info->rolling || !entry->encoder_ops) {
            printf("Protocol '%s' is decode-only (rolling code safety).\r\n", protoname);
            return;
        }
        char *end = NULL;
        errno = 0;
        uint64_t key = strtoull(keytext, &end, 0);
        if (errno == ERANGE || !end || end == keytext || *end != '\0') {
            printf("Key must be a decimal or 0x-prefixed hexadecimal value.\r\n");
            return;
        }
        if (!bits || bits > 64 || !repeat || repeat > 20) {
            printf("Limits: bits 1-64, repeats 1-20.\r\n");
            return;
        }
        if (entry->info->bit_min && bits < entry->info->bit_min) {
            printf("Protocol '%s' needs at least %lu bits.\r\n", protoname,
                   (unsigned long)entry->info->bit_min);
            return;
        }
        if (entry->info->bit_max && bits > entry->info->bit_max) {
            printf("Protocol '%s' supports at most %lu bits.\r\n", protoname,
                   (unsigned long)entry->info->bit_max);
            return;
        }
        if (bits < 64 && (key >> bits)) {
            printf("Key does not fit in %u bits.\r\n", bits);
            return;
        }
        subghz_protocol_encoder_t *enc = subghz_encoder_alloc(entry);
        if (!enc) {
            printf("Encoder init failed.\r\n");
            return;
        }
        if (te >= 50u && te <= 2000u) {
            enc->te_us = te; /* TE hint for TE-flexible encoders. */
        }
        if (strcmp(protoname, "RcSwitch") == 0) {
            uint32_t proto_num = 1;
            if (bp_cmd_get_uint32(&send_def, 'o', &proto_num)) {
                if (proto_num == 1 || proto_num == 2 || proto_num == 11) {
                    subghz_rcswitch_set_proto(enc, (uint8_t)proto_num);
                }
            }
        }
        if (subghz_encoder_feed(enc, key, bits) != SUBGHZ_PROTOCOL_DONE) {
            printf("Encoder rejected key/bits for '%s'.\r\n", protoname);
            subghz_encoder_free(enc);
            return;
        }
        printf("Transmitting %s at %u.%03uMHz, 0x%08lx%08lx/%u bits, repeats=%u.\r\n",
               protoname, mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
               (unsigned long)(key >> 32), (unsigned long)key, bits, repeat);
        if (!transmit_upload(enc, repeat)) {
            printf("Transmit failed: CC1101 state transition error.\r\n");
        }
        subghz_encoder_free(enc);
        return;
    }

    char payload_text[32];
    if (!bp_cmd_get_positional_string(&send_def, 1, payload_text, sizeof(payload_text))) {
        bp_cmd_help_show(&send_def); return;
    }
    char *end = NULL;
    errno = 0;
    uint64_t payload = strtoull(payload_text, &end, 0);
    if (errno == ERANGE || !end || end == payload_text || *end != '\0') {
        printf("Payload must be a decimal or 0x-prefixed hexadecimal value up to 64 bits.\r\n"); return;
    }
    if (!bits || bits > 64 || te < 50 || te > 2000 || !repeat || repeat > 20) {
        printf("Limits: bits 1-64, TE 50-2000us, repeats 1-20.\r\n"); return;
    }
    if (bits < 64 && (payload >> bits)) {
        printf("Payload does not fit in %u bits.\r\n", bits); return;
    }
    const subghz_protocol_entry_t *princeton = subghz_protocol_find("Princeton");
    if (princeton && princeton->encoder_ops && te == 350u) {
        /* Route through the Princeton encoder so TX matches the decoder. */
        subghz_protocol_encoder_t *enc = subghz_encoder_alloc(princeton);
        if (enc && subghz_encoder_feed(enc, payload, bits) == SUBGHZ_PROTOCOL_DONE) {
            printf("Transmitting Princeton OOK at %u.%03uMHz, 0x%08lx%08lx/%u bits, TE=%uus, repeats=%u.\r\n",
                   mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
                   (unsigned long)(payload >> 32), (unsigned long)payload, bits, te, repeat);
            if (!transmit_upload(enc, repeat)) {
                printf("Transmit failed: CC1101 state transition error.\r\n");
            }
            subghz_encoder_free(enc);
            return;
        }
        if (enc) {
            subghz_encoder_free(enc);
        }
    }
    printf("Transmitting Princeton OOK at %u.%03uMHz, 0x%08lx%08lx/%u bits, TE=%uus, repeats=%u.\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           (unsigned long)(payload >> 32), (unsigned long)payload, bits, te, repeat);
    if (!cc1101_idle()) { printf("Failed to enter IDLE state.\r\n"); return; }
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
    apply_tx_power();
    if (!cc1101_tx()) {
        bio_input(SUBGHZ_GDO0);
        (void)cc1101_rx();
        printf("Failed to enter TX state.\r\n");
        return;
    }
    sleep_ms(1);
    for (uint32_t r = 0; r < repeat; r++) {
        for (int bit = (int)bits - 1; bit >= 0; bit--) {
            if ((payload >> bit) & 1ull) { pulse(true, te * 3); pulse(false, te); }
            else { pulse(true, te); pulse(false, te * 3); }
        }
        pulse(true, te); pulse(false, te * 30); /* Flipper Princeton stop and guard. */
    }
    (void)cc1101_idle();
    bio_input(SUBGHZ_GDO0);
    if (!cc1101_rx()) printf("Warning: failed to return to RX state.\r\n");
}

static void burst_handler(struct command_result *res) {
    if (bp_cmd_help_check(&burst_def, res->help_flag) || !command_ready()) return;
    if (!tx_guard()) return;
    uint32_t count = 1;
    if (bp_cmd_get_uint32(&burst_def, 'c', &count)) {
        if (count == 0 || count > 100) count = 1;
    }
    uint32_t freq = mode_config.frequency_khz;
    if (bp_cmd_get_uint32(&burst_def, 'f', &freq)) {
        if (!valid_frequency(freq)) {
            printf("Invalid frequency %lu kHz.\r\n", (unsigned long)freq);
            return;
        }
    }
    printf("Transmitting %lu RF jitter burst(s) at %u.%03u MHz...\r\n",
           (unsigned long)count, freq / 1000, freq % 1000);
    uint32_t old_freq = mode_config.frequency_khz;
    if (freq != old_freq) {
        if (!cc1101_set_frequency(freq)) {
            printf("Failed to set frequency.\r\n");
            return;
        }
    }
    if (!cc1101_idle()) {
        printf("CC1101 state transition failed.\r\n");
        if (freq != old_freq) (void)cc1101_set_frequency(old_freq);
        return;
    }
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
    apply_tx_power();
    if (!cc1101_tx()) {
        bio_input(SUBGHZ_GDO0);
        (void)cc1101_rx();
        printf("CC1101 TX transition failed.\r\n");
        if (freq != old_freq) (void)cc1101_set_frequency(old_freq);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        send_random_burst();
        if (i + 1 < count) sleep_ms(5);
    }
    bio_put(SUBGHZ_GDO0, false);
    cc1101_idle();
    bio_input(SUBGHZ_GDO0);
    if (freq != old_freq) {
        (void)cc1101_set_frequency(old_freq);
    }
    (void)cc1101_rx();
    printf("Burst transmission complete.\r\n");
}

/* ============================================================
 * New commands: preset, protocol, hop, power, bitrate, info
 * ============================================================ */

/* Parse "Custom_preset_data: 02 0D 03 47 ..." hex bytes into addr/val pairs. */
static bool parse_custom_preset(const char *line, uint8_t *pairs, size_t *pair_count) {
    uint8_t pa[CC1101_CUSTOM_PA_SIZE];
    bool have_pa = false;
    return subghz_parse_custom_preset_full(line, pairs, pair_count, CC1101_CUSTOM_MAX_PAIRS,
                                           pa, &have_pa);
}

static bool parse_custom_preset_full(const char *line, uint8_t *pairs, size_t *pair_count,
                                     uint8_t *pa_table, bool *have_pa) {
    return subghz_parse_custom_preset_full(line, pairs, pair_count, CC1101_CUSTOM_MAX_PAIRS,
                                           pa_table, have_pa);
}

/* Canonical Flipper preset names for .sub files (record/savekey output). */
static const char *preset_to_flipper_name(cc1101_preset_t preset) {
    switch (preset) {
    case CC1101_PRESET_OOK_270:
        return "FuriHalSubGhzPresetOok270Async";
    case CC1101_PRESET_OOK_650:
        return "FuriHalSubGhzPresetOok650Async";
    case CC1101_PRESET_2FSK_DEV238:
        return "FuriHalSubGhzPreset2FSKDev238Async";
    case CC1101_PRESET_2FSK_DEV12K:
        return "FuriHalSubGhzPreset2FSKDev12KAsync";
    case CC1101_PRESET_2FSK_DEV476:
        return "FuriHalSubGhzPreset2FSKDev476Async";
    case CC1101_PRESET_GFSK_10K:
        return "FuriHalSubGhzPresetGFSK9_99KbAsync";
    case CC1101_PRESET_MSK_100K:
        return "FuriHalSubGhzPresetMSK99_97KbAsync";
    case CC1101_PRESET_CUSTOM:
        return "FuriHalSubGhzPresetCustom";
    default:
        return "FuriHalSubGhzPresetOok270Async";
    }
}

static void preset_handler(struct command_result *res) {
    if (bp_cmd_help_check(&preset_def, res->help_flag)) return;
    char name[32];
    if (!bp_cmd_get_positional_string(&preset_def, 1, name, sizeof(name))) {
        /* List available presets. */
        printf("Available presets:\r\n");
        for (uint i = 0; i < CC1101_PRESET_COUNT; i++) {
            const cc1101_preset_info_t *info = cc1101_preset_info((cc1101_preset_t)i);
            printf("  %s - %s%s\r\n", info->name, info->description,
                   i == mode_config.preset ? " (active)" : "");
        }
        return;
    }
    if (!strcmp(name, "import") || !strcmp(name, "export")) {
        char file[BP_FILENAME_MAX] = { 0 };
        if (!bp_cmd_get_positional_string(&preset_def, 2, file, sizeof(file))) {
            printf("Usage: preset %s <file>\r\n", name);
            return;
        }
        if (!command_ready()) return;
        if (!strcmp(name, "import")) {
            FIL f;
            FRESULT fr = f_open(&f, file, FA_READ);
            if (fr != FR_OK) {
                printf("Unable to open %s (FatFs error %u).\r\n", file, fr);
                return;
            }
            uint8_t pairs[CC1101_CUSTOM_MAX_PAIRS * 2];
            size_t npairs = 0;
            uint8_t pa[CC1101_CUSTOM_PA_SIZE];
            bool have_pa = false, found = false;
            char line[256];
            while (f_gets(line, sizeof(line), &f)) {
                if (!strncmp(line, "Custom_preset_data:", 19) &&
                    parse_custom_preset_full(line, pairs, &npairs, pa, &have_pa)) {
                    found = true;
                    break;
                }
            }
            f_close(&f);
            if (!found) {
                printf("No Custom_preset_data in %s.\r\n", file);
                return;
            }
            if (!cc1101_custom_preset_set_full(pairs, npairs, have_pa ? pa : NULL) ||
                !cc1101_set_preset(CC1101_PRESET_CUSTOM) || !cc1101_rx()) {
                printf("Failed to apply custom preset.\r\n");
                return;
            }
            mode_config.preset = CC1101_PRESET_CUSTOM;
            mode_config.bandwidth_khz = 0;
            printf("Custom preset applied (%u registers%s).\r\n", (unsigned)npairs,
                   have_pa ? ", PA table" : "");
            return;
        }
        /* Export: Flipper-exact register pairs + 00 00 terminator + PA table. */
        uint8_t regs[0x2f];
        uint8_t pa[CC1101_CUSTOM_PA_SIZE];
        if (!cc1101_read_config(0x00, regs, sizeof(regs)) ||
            !cc1101_read_patable(pa)) {
            printf("Failed to read CC1101 registers.\r\n");
            return;
        }
        FIL f;
        FRESULT fr = f_open(&f, file, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
            printf("Unable to create %s (FatFs error %u).\r\n", file, fr);
            return;
        }
        bool ok = f_printf(&f, "Filetype: Flipper SubGhz Setting File\nVersion: 1\n"
                               "Custom_preset_module: CC1101\nCustom_preset_data:") >= 0;
        for (uint a = 0; a < sizeof(regs) && ok; a++) {
            if (f_printf(&f, " %02X %02X", a, regs[a]) < 0) {
                ok = false;
            }
        }
        /* Register-block terminator + 8-byte PA table (Flipper format). */
        if (ok && f_printf(&f, " 00 00") < 0) {
            ok = false;
        }
        for (uint i = 0; i < sizeof(pa) && ok; i++) {
            if (f_printf(&f, " %02X", pa[i]) < 0) {
                ok = false;
            }
        }
        if (ok && f_puts("\n", &f) < 0) {
            ok = false;
        }
        fr = f_close(&f);
        printf(ok && fr == FR_OK ? "Custom preset exported to %s.\r\n" : "Export failed.\r\n",
               file);
        return;
    }
    cc1101_preset_t p = cc1101_preset_from_name(name);
    if (p >= CC1101_PRESET_COUNT) {
        printf("Unknown preset '%s'. Use 'preset' (no args) to list available presets.\r\n", name);
        return;
    }
    if (!command_ready()) return;
    if (p == CC1101_PRESET_CUSTOM && !cc1101_custom_preset_get(NULL, 0)) {
        printf("No custom preset loaded. Use 'preset import <file>' first.\r\n");
        return;
    }
    if (!cc1101_set_preset(p)) {
        printf("Failed to apply preset %s.\r\n", name);
        return;
    }
    if (!cc1101_rx()) {
        printf("Failed to enter RX state after preset change.\r\n");
        return;
    }
    mode_config.preset = p;
    mode_config.bandwidth_khz = cc1101_preset_is_ook(p) ? (p == CC1101_PRESET_OOK_650 ? 650 : 270) : 0;
    printf("Preset: %s (%s)\r\n", cc1101_preset_name(p), cc1101_preset_info(p)->description);
}

static void protocol_handler(struct command_result *res) {
    if (bp_cmd_help_check(&protocol_def, res->help_flag)) return;
    char name[32];
    if (!bp_cmd_get_positional_string(&protocol_def, 1, name, sizeof(name))) {
        /* List available protocols. */
        uint32_t count = subghz_protocol_count();
        printf("Available protocols (%lu, * = TX capable):\r\n", (unsigned long)count);
        for (uint32_t i = 0; i < count; i++) {
            const subghz_protocol_entry_t *entry = subghz_protocol_find_by_index(i);
            if (entry) {
                printf("  %s%s - %s\r\n", entry->info->rolling ? " " : "*", entry->info->name,
                       entry->info->description);
            }
        }
        return;
    }
    const subghz_protocol_entry_t *entry = subghz_protocol_find(name);
    if (!entry) {
        printf("Unknown protocol '%s'. Use 'protocol' (no args) to list available protocols.\r\n", name);
        return;
    }
    if (!command_ready()) return;
    printf("Protocol: %s (%s)\r\n", entry->info->name, entry->info->description);
    if (entry->info->bit_min || entry->info->bit_max) {
        printf("Bits: %lu-%lu, default frequency %u.%03u MHz.\r\n",
               (unsigned long)entry->info->bit_min, (unsigned long)entry->info->bit_max,
               entry->info->frequency_default_khz / 1000, entry->info->frequency_default_khz % 1000);
    }
    if (entry->info->rolling) {
        printf("Rolling code: decode/display only, TX refused.\r\n");
    } else {
        printf("Use 'receive' to capture or 'send -P %s -k <hexkey>' to transmit.\r\n",
               entry->info->name);
    }
}

/* Persist the hop list as "khz,khz,..." text (Flipper hopper list equivalent). */
static void hop_save(void) {
    FIL file;
    FRESULT fr = f_open(&file, SUBGHZ_HOP_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        printf("Unable to save hop list (FatFs error %u).\r\n", fr);
        return;
    }
    bool ok = true;
    for (uint32_t i = 0; i < mode_config.hop_count && ok; i++) {
        if (f_printf(&file, "%s%lu", i ? "," : "", (unsigned long)mode_config.hop_frequencies[i]) <
            0) {
            ok = false;
        }
    }
    if (ok && f_puts("\n", &file) < 0) {
        ok = false;
    }
    fr = f_close(&file);
    printf(ok && fr == FR_OK ? "Hop list saved.\r\n" : "Hop list save failed.\r\n");
}

static void hop_load(void) {
    FIL file;
    if (f_open(&file, SUBGHZ_HOP_FILE, FA_READ) != FR_OK) {
        return;
    }
    char line[128];
    if (!f_gets(line, sizeof(line), &file)) {
        f_close(&file);
        return;
    }
    f_close(&file);
    uint32_t count = 0;
    uint32_t freqs[SUBGHZ_MAX_HOP];
    char *cursor = line;
    while (*cursor && count < SUBGHZ_MAX_HOP) {
        while (*cursor == ' ' || *cursor == ',') cursor++;
        if (!*cursor || *cursor == '\r' || *cursor == '\n') break;
        char *end = NULL;
        unsigned long khz = strtoul(cursor, &end, 10);
        if (end == cursor || !valid_frequency((uint32_t)khz)) return;
        freqs[count++] = (uint32_t)khz;
        cursor = end;
    }
    if (count >= 2) {
        memcpy(mode_config.hop_frequencies, freqs, sizeof(freqs[0]) * count);
        mode_config.hop_count = count;
    }
}

static void hop_handler(struct command_result *res) {
    if (bp_cmd_help_check(&hop_def, res->help_flag) || !command_ready()) return;
    char freq_list[128];
    uint32_t dwell = 500, value;
    if (!bp_cmd_get_string(&hop_def, 'f', freq_list, sizeof(freq_list))) {
        if (mode_config.hop_count == 0) {
            printf("No hop frequencies configured. Use 'hop -f freq1,freq2,...' to set them.\r\n");
            return;
        }
        /* Run hopping capture with existing frequencies. */
        printf("Hopping capture with %lu preconfigured frequencies.\r\n", (unsigned long)mode_config.hop_count);
        if (bp_cmd_get_uint32(&hop_def, 'd', &value)) dwell = value;
        if (!dwell || dwell > 10000) { printf("Dwell must be 1-10000ms.\r\n"); return; }
        capture_pulses_hop(dwell);
        printf("\r\nCaptured %u pulses via frequency hopping.\r\n", capture_count);
        analyze_capture();
        run_protocol_decoders();
        return;
    }
    if (bp_cmd_get_uint32(&hop_def, 'd', &value)) dwell = value;
    if (!dwell || dwell > 10000) { printf("Dwell must be 1-10000ms.\r\n"); return; }

    /* Parse comma-separated frequency list. */
    mode_config.hop_count = 0;
    char *cursor = freq_list;
    while (*cursor && mode_config.hop_count < SUBGHZ_MAX_HOP) {
        while (*cursor == ' ' || *cursor == ',') cursor++;
        if (!*cursor) break;
        char *end = NULL;
        unsigned long khz = strtoul(cursor, &end, 10);
        if (end == cursor || !valid_frequency((uint32_t)khz)) {
            printf("Invalid frequency in hop list.\r\n");
            mode_config.hop_count = 0;
            return;
        }
        mode_config.hop_frequencies[mode_config.hop_count++] = (uint32_t)khz;
        cursor = end;
    }
    if (mode_config.hop_count < 2) {
        printf("Need at least 2 valid frequencies for hopping.\r\n");
        mode_config.hop_count = 0;
        return;
    }
    printf("Hop list (%lu frequencies):", (unsigned long)mode_config.hop_count);
    for (uint32_t i = 0; i < mode_config.hop_count; i++) {
        printf(" %u.%03u", mode_config.hop_frequencies[i] / 1000, mode_config.hop_frequencies[i] % 1000);
    }
    printf(" MHz\r\n");
    if (bp_cmd_find_flag(&hop_def, 's')) {
        hop_save();
    }
    printf("Starting hopping capture, dwell=%lums (press key to stop)...\r\n", (unsigned long)dwell);
    capture_pulses_hop(dwell);
    printf("\r\nCaptured %u pulses via frequency hopping.\r\n", capture_count);
    analyze_capture();
    run_protocol_decoders();
}

static void power_handler(struct command_result *res) {
    if (bp_cmd_help_check(&power_def, res->help_flag) || !command_ready()) return;
    uint32_t value;
    bool ook = cc1101_preset_is_ook((cc1101_preset_t)mode_config.preset);
    char dbm_text[16] = { 0 };
    if (bp_cmd_get_string(&power_def, 'd', dbm_text, sizeof(dbm_text))) {
        char *end = NULL;
        errno = 0;
        long dbm = strtol(dbm_text, &end, 10);
        if (errno == ERANGE || !end || end == dbm_text || *end != '\0' || dbm < -30 || dbm > 12) {
            printf("TX power must be -30..12 dBm.\r\n");
            return;
        }
        if (!cc1101_set_tx_power_dbm((int8_t)dbm, ook)) {
            printf("Failed to set TX power.\r\n");
            return;
        }
        mode_config.tx_power_dbm = dbm;
        mode_config.tx_power = 0;
        printf("TX power: %ld dBm (%s modulation).\r\n", (long)dbm, ook ? "OOK" : "FSK");
        return;
    }
    uint32_t level = 0;
    if (bp_cmd_get_uint32(&power_def, 'l', &value)) level = value;
    if (level > 255) { printf("TX power must be 0-255.\r\n"); return; }
    mode_config.tx_power = level;
    mode_config.tx_power_dbm = 127;
    if (level == 0) {
        /* Restore the selected band's conservative default immediately. */
        int8_t default_dbm = cc1101_tx_power_dbm_default(mode_config.frequency_khz);
        if (!cc1101_set_tx_power_dbm(default_dbm, ook)) {
            printf("Failed to restore default TX power.\r\n");
            return;
        }
        mode_config.tx_power_dbm = default_dbm;
        printf("TX power reset to default per-band values.\r\n");
        return;
    }
    if (!cc1101_set_tx_power((uint8_t)level)) {
        printf("Failed to set TX power.\r\n");
        return;
    }
    printf("TX power set to raw %lu.\r\n", (unsigned long)level);
}

static void txallow_handler(struct command_result *res) {
    if (bp_cmd_help_check(&txallow_def, res->help_flag)) return;
    char policy[16];
    if (!bp_cmd_get_positional_string(&txallow_def, 1, policy, sizeof(policy))) {
        printf("TX policy: %s\r\n", mode_config.tx_policy == SUBGHZ_TX_OPEN ? "open" : "strict");
        printf("Strict allows TX on 314.5-315.5, 433.05-434.79, 868.0-868.6, 902-928 MHz.\r\n");
        printf("Current frequency %u.%03u MHz: TX %s.\r\n", mode_config.frequency_khz / 1000,
               mode_config.frequency_khz % 1000, tx_allowed_here() ? "allowed" : "blocked");
        return;
    }
    if (!strcmp(policy, "strict")) {
        mode_config.tx_policy = SUBGHZ_TX_STRICT;
    } else if (!strcmp(policy, "open")) {
        printf("Warning: open policy permits TX anywhere the CC1101 tunes.\r\n");
        printf("You are responsible for band, duty-cycle, and licensing rules.\r\n");
        mode_config.tx_policy = SUBGHZ_TX_OPEN;
    } else {
        printf("Use 'txallow strict' or 'txallow open'.\r\n");
        return;
    }
    printf("TX policy: %s.\r\n", mode_config.tx_policy == SUBGHZ_TX_OPEN ? "open" : "strict");
}

static void bitrate_handler(struct command_result *res) {
    if (bp_cmd_help_check(&bitrate_def, res->help_flag) || !command_ready()) return;
    uint32_t baud = 0, value;
    if (bp_cmd_get_uint32(&bitrate_def, 'b', &value)) baud = value;
    if (baud < 600 || baud > 500000) {
        printf("Bitrate must be 600-500000 baud.\r\n");
        return;
    }
    if (!cc1101_set_bitrate(baud)) {
        printf("Failed to set bitrate.\r\n");
        return;
    }
    mode_config.bitrate_baud = baud;
    printf("Bitrate: %lu baud\r\n", (unsigned long)baud);
}

static void info_handler(struct command_result *res) {
    if (bp_cmd_help_check(&info_def, res->help_flag) || !command_ready()) return;
    const cc1101_preset_info_t *pi = cc1101_preset_info((cc1101_preset_t)mode_config.preset);
    printf("Frequency: %u.%03u MHz\r\n", mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
    printf("Preset:    %s (%s)\r\n", pi ? pi->name : "?", pi ? pi->description : "?");
    printf("Modulation: %s\r\n", cc1101_preset_is_ook((cc1101_preset_t)mode_config.preset) ? "OOK" :
           cc1101_preset_is_fsk((cc1101_preset_t)mode_config.preset) ? "FSK/MSK/GFSK" : "Unknown");
    printf("GDO0:     BIO%u\r\n", (unsigned)mode_config.gdo_pin);
    printf("TX:       %s, policy %s\r\n", tx_allowed_here() ? "allowed" : "blocked",
           mode_config.tx_policy == SUBGHZ_TX_OPEN ? "open" : "strict");
    if (mode_config.bitrate_baud) printf("Bitrate:  %lu baud\r\n", (unsigned long)mode_config.bitrate_baud);
    if (mode_config.tx_power) printf("TX Power: %lu\r\n", (unsigned long)mode_config.tx_power);
    if (mode_config.hop_count) {
        printf("Hopping:  %lu frequencies\r\n", (unsigned long)mode_config.hop_count);
    }
    printf("RSSI:     %d dBm\r\n", cc1101_rssi());
}

/* ============================================================
 * Command table
 * ============================================================ */

const struct _mode_command_struct subghz_commands[] = {
    { .func = setfrequency_handler, .def = &frequency_def, .supress_fala_capture = true },
    { .func = rssi_handler, .def = &rssi_def, .supress_fala_capture = true },
    { .func = preset_handler, .def = &preset_def, .supress_fala_capture = true },
    { .func = protocol_handler, .def = &protocol_def, .supress_fala_capture = true },
    { .func = info_handler, .def = &info_def, .supress_fala_capture = true },
    { .func = scan_handler, .def = &scan_def, .supress_fala_capture = true },
    { .func = sweep_handler, .def = &sweep_def, .supress_fala_capture = true },
    { .func = trace_handler, .def = &trace_def, .supress_fala_capture = true },
    { .func = waterfall_handler, .def = &waterfall_def, .supress_fala_capture = true },
    { .func = receive_handler, .def = &receive_def, .supress_fala_capture = true },
    { .func = hop_handler, .def = &hop_def, .supress_fala_capture = true },
    { .func = replay_handler, .def = &replay_def, .supress_fala_capture = true },
    { .func = record_handler, .def = &record_def, .supress_fala_capture = true },
    { .func = load_handler, .def = &load_def, .supress_fala_capture = true },
    { .func = send_handler, .def = &send_def, .supress_fala_capture = true },
    { .func = decode_handler, .def = &decode_def, .supress_fala_capture = true },
    { .func = clone_handler, .def = &clone_def, .supress_fala_capture = true },
    { .func = savekey_handler, .def = &savekey_def, .supress_fala_capture = true },
    { .func = monitor_handler, .def = &monitor_def, .supress_fala_capture = true },
    { .func = keyload_handler, .def = &keyload_def, .supress_fala_capture = true },
    { .func = keys_handler, .def = &keys_def, .supress_fala_capture = true },
    { .func = keyadd_handler, .def = &keyadd_def, .supress_fala_capture = true },
    { .func = txallow_handler, .def = &txallow_def, .supress_fala_capture = true },
    { .func = power_handler, .def = &power_def, .supress_fala_capture = true },
    { .func = bitrate_handler, .def = &bitrate_def, .supress_fala_capture = true },
    { .func = burst_handler, .def = &burst_def, .supress_fala_capture = true },
};
const uint32_t subghz_commands_count = count_of(subghz_commands);

/* ============================================================
 * Setup
 * ============================================================ */

uint32_t subghz_setup(void) {
    const char config_file[] = "bpsubghz.bp";
    uint32_t tx_power_dbm_u = 127;
    if (mode_config.tx_power_dbm != 127) {
        /* Persisted as unsigned with 127 = default marker. */
        tx_power_dbm_u = (uint32_t)(mode_config.tx_power_dbm + 128);
    }
    const mode_config_t config[] = {
        { "$.frequency_khz", &mode_config.frequency_khz, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.gdo_pin", &mode_config.gdo_pin, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.preset", &mode_config.preset, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.bandwidth_khz", &mode_config.bandwidth_khz, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.tx_power", &mode_config.tx_power, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.tx_power_dbm", &tx_power_dbm_u, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.tx_policy", &mode_config.tx_policy, MODE_CONFIG_FORMAT_DECIMAL },
    };
    uint32_t frequency, gdo, preset_val;
    bp_cmd_status_t status = bp_cmd_flag(&subghz_setup_def, 'f', &frequency);
    if (status == BP_CMD_INVALID) return 0;
    if (status == BP_CMD_MISSING) {
        if (!storage_load_mode(config_file, config, count_of(config)) ||
            !valid_frequency(mode_config.frequency_khz) || mode_config.gdo_pin > BIO3 ||
            mode_config.preset >= CC1101_PRESET_COUNT) {
            mode_config.frequency_khz = SUBGHZ_DEFAULT_KHZ;
            mode_config.gdo_pin = BIO0;
            mode_config.preset = CC1101_PRESET_OOK_270;
            mode_config.bandwidth_khz = 270;
            mode_config.tx_power = 0;
            mode_config.tx_power_dbm = 127;
            mode_config.tx_policy = SUBGHZ_TX_STRICT;
        }
        if (tx_power_dbm_u == 127) {
            mode_config.tx_power_dbm = 127;
        } else {
            mode_config.tx_power_dbm = (int32_t)tx_power_dbm_u - 128;
        }
        if (mode_config.tx_policy > SUBGHZ_TX_OPEN) {
            mode_config.tx_policy = SUBGHZ_TX_STRICT;
        }
        if (bp_cmd_prompt(&frequency_range, &frequency) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&gdo_range, &gdo) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&preset_choice, &preset_val) != BP_CMD_OK) return 0;
        mode_config.preset = preset_val;
    } else {
        status = bp_cmd_flag(&subghz_setup_def, 'g', &gdo);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) gdo = BIO0;
        status = bp_cmd_flag(&subghz_setup_def, 'p', &preset_val);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) {
            /* Keep current/default preset. */
        } else {
            mode_config.preset = preset_val;
        }
    }
    if (!valid_frequency(frequency)) {
        printf("Supported bands: 300-348, 387-464, and 779-928 MHz.\r\n"); return 0;
    }
    if (gdo > BIO3) { printf("GDO0 must use BIO0, BIO1, BIO2, or BIO3.\r\n"); return 0; }
    mode_config.frequency_khz = frequency;
    mode_config.gdo_pin = gdo;
    tx_power_dbm_u = (mode_config.tx_power_dbm == 127) ? 127u
                                                       : (uint32_t)(mode_config.tx_power_dbm + 128);
    storage_save_mode(config_file, config, count_of(config));
    return 1;
}

uint32_t subghz_setup_exc(void) {
    system_bio_update_purpose_and_label(true, SUBGHZ_GDO0, BP_PIN_MODE, pin_labels[0]);
    system_bio_update_purpose_and_label(true, M_SPI_CDI, BP_PIN_MODE, pin_labels[1]);
    system_bio_update_purpose_and_label(true, M_SPI_CS, BP_PIN_MODE, pin_labels[2]);
    system_bio_update_purpose_and_label(true, M_SPI_CLK, BP_PIN_MODE, pin_labels[3]);
    system_bio_update_purpose_and_label(true, M_SPI_CDO, BP_PIN_MODE, pin_labels[4]);
    bio_input(SUBGHZ_GDO0);
    mode_config.initialized = cc1101_init(mode_config.frequency_khz);
    uint8_t part = 0xff, version = 0xff;
    if (!mode_config.initialized) {
        printf("\r\nCC1101 not detected. Check 3.3V and wiring.\r\n");
        subghz_cleanup();
        return 0;
    }
    if (!cc1101_set_preset((cc1101_preset_t)mode_config.preset)) {
        printf("\r\nInvalid preset.\r\n");
        subghz_cleanup();
        return 0;
    }
    apply_tx_power();
    if (!cc1101_rx()) {
        printf("\r\nFailed to enter RX state.\r\n");
        subghz_cleanup();
        return 0;
    }
    hop_load();
    subghz_keystore_init(&keystore);
    subghz_keeloq_keystore_set(&keystore);
    keystore_loaded = false;
    cc1101_present(&part, &version);
    const cc1101_preset_info_t *pi = cc1101_preset_info((cc1101_preset_t)mode_config.preset);
    printf("\r\nCC1101 detected (VERSION=0x%02x), tuned to %u.%03uMHz, %s (%s).\r\n", version,
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           pi ? pi->name : "?", pi ? pi->description : "?");
    subghz_protocols_init();
    return 1;
}

void subghz_cleanup(void) {
    subghz_cap_stop();
    if (mode_config.initialized) cc1101_deinit();
    mode_config.initialized = 0;
    last_decode.valid = false;
    const uint8_t pins[] = { SUBGHZ_GDO0, M_SPI_CDI, M_SPI_CS, M_SPI_CLK, M_SPI_CDO };
    for (uint i = 0; i < count_of(pins); i++) system_bio_update_purpose_and_label(false, pins[i], BP_PIN_MODE, 0);
}

void subghz_settings(void) {
    const cc1101_preset_info_t *pi = cc1101_preset_info((cc1101_preset_t)mode_config.preset);
    printf("CC1101 frequency: %u.%03u MHz\r\n", mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
    printf("CC1101 preset: %s\r\n", pi ? pi->name : "?");
    printf("CC1101 modulation: %s\r\n", cc1101_preset_is_ook((cc1101_preset_t)mode_config.preset) ? "OOK" :
           cc1101_preset_is_fsk((cc1101_preset_t)mode_config.preset) ? "FSK/MSK/GFSK" : "Unknown");
    printf("CC1101 GDO0: BIO%u\r\n", (unsigned)mode_config.gdo_pin);
    printf("TX policy: %s (TX %s at current frequency)\r\n",
           mode_config.tx_policy == SUBGHZ_TX_OPEN ? "open" : "strict",
           tx_allowed_here() ? "allowed" : "blocked");
    if (mode_config.tx_power_dbm != 127) {
        printf("TX power: %ld dBm\r\n", (long)mode_config.tx_power_dbm);
    } else if (mode_config.tx_power) {
        printf("TX power: raw %lu\r\n", (unsigned long)mode_config.tx_power);
    }
    if (mode_config.hop_count) {
        printf("CC1101 hop list: %lu frequencies\r\n", (unsigned long)mode_config.hop_count);
    }
    printf("Protocols: %lu (%lu TX capable), keystore: %lu keys\r\n",
           (unsigned long)subghz_protocol_count(),
           (unsigned long)subghz_protocol_count_static(), (unsigned long)keystore.count);
}

void subghz_help(void) {
    printf("CC1101 Sub-GHz radio mode. Supports OOK, FSK, GFSK, and MSK modulation.\r\n\r\n");
    printf("Connection: BIO6=SCK, BIO7=MOSI, BIO4=MISO, BIO5=CS, BIO%u=GDO0, GND=GND.\r\n",
           mode_config.gdo_pin);
    printf("Connect CC1101 VCC directly to 3.3V. Attach an antenna suitable for the selected band.\r\n");
    printf("Transmit only on frequencies and duty cycles permitted in your jurisdiction.\r\n\r\n");
    ui_help_mode_commands(subghz_commands, subghz_commands_count);
}

bool subghz_preflight_sanity_check(void) { return ui_help_sanity_check(true, 0x00); }
