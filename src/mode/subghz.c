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

#define SUBGHZ_DEFAULT_KHZ 433920u

static struct {
    uint32_t frequency_khz;
    uint32_t gdo_pin;
    uint32_t bandwidth_khz;
    uint32_t initialized;
} mode_config = { SUBGHZ_DEFAULT_KHZ, BIO0, 270, 0 };

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

static bool field_value_equals(const char *line, size_t prefix_length, const char *expected) {
    const char *value = line + prefix_length;
    while (*value == ' ' || *value == '\t') value++;
    size_t length = strcspn(value, "\r\n");
    return strlen(expected) == length && memcmp(value, expected, length) == 0;
}

static bool parse_frequency_field(const char *line, uint32_t *frequency_khz) {
    const char *value = line + 10;
    char *end = NULL;
    errno = 0;
    unsigned long frequency_hz = strtoul(value, &end, 10);
    if (errno == ERANGE || end == value || frequency_hz > UINT32_MAX || frequency_hz % 1000u) return false;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\r' && *end != '\n' && *end != '\0') return false;
    uint32_t khz = (uint32_t)(frequency_hz / 1000u);
    if (!valid_frequency(khz)) return false;
    *frequency_khz = khz;
    return true;
}

static void frequency_band_bounds(uint32_t khz, uint32_t *low, uint32_t *high) {
    if (khz <= 348000u) { *low = 300000u; *high = 348000u; }
    else if (khz <= 464000u) { *low = 387000u; *high = 464000u; }
    else { *low = 779000u; *high = 928000u; }
}

static const bp_val_constraint_t frequency_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 300000, .max = 928000, .def = SUBGHZ_DEFAULT_KHZ },
};
static const bp_val_constraint_t gdo_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = BIO0, .max = BIO3, .def = BIO0 },
};
static const bp_val_choice_t bandwidth_choices[] = {
    { "270", NULL, 0, 270 },
    { "650", NULL, 0, 650 },
};
static const bp_val_constraint_t bandwidth_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = bandwidth_choices, .count = 2, .def = 0 },
};
static const bp_command_opt_t setup_opts[] = {
    { "frequency", 'f', BP_ARG_REQUIRED, "kHz", 0, &frequency_range },
    { "gdo", 'g', BP_ARG_REQUIRED, "BIO0-BIO3", 0, &gdo_range },
    { "bandwidth", 'w', BP_ARG_REQUIRED, "270|650", 0, &bandwidth_choice }, { 0 }
};
const bp_command_def_t subghz_setup_def = {
    .name = "subghz", .description = 0, .opts = setup_opts,
};

static const bp_command_positional_t frequency_positionals[] = {
    { "kHz", "300000-928000 (supported CC1101 bands)", 0, true }, { 0 }
};
static const char *const frequency_usage[] = {
    "setfrequency <kHz>", "Tune to 433.92 MHz:%s setfrequency 433920"
};
static const bp_command_def_t frequency_def = {
    .name = "setfrequency", .description = 0,
    .positionals = frequency_positionals, .positional_count = 1,
    .usage = frequency_usage, .usage_count = count_of(frequency_usage),
};

static const bp_command_def_t rssi_def = { .name = "rssi", .description = 0 };

static const bp_command_opt_t scan_opts[] = {
    { "start", 'a', BP_ARG_REQUIRED, "kHz", 0 },
    { "stop",  'z', BP_ARG_REQUIRED, "kHz", 0 },
    { "step",  's', BP_ARG_REQUIRED, "kHz", 0 },
    { "dwell", 'd', BP_ARG_REQUIRED, "ms", 0 }, { 0 }
};
static const char *const scan_usage[] = {
    "scan [-a start_kHz] [-z stop_kHz] [-s step_kHz] [-d dwell_ms]",
    "Scan around the current frequency:%s scan",
    "Scan the 433 MHz ISM band:%s scan -a 433050 -z 434790 -s 25 -d 10"
};
static const bp_command_def_t scan_def = {
    .name = "scan", .description = 0, .opts = scan_opts,
    .usage = scan_usage, .usage_count = count_of(scan_usage),
};

static const bp_command_opt_t receive_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "ms", 0 }, { 0 }
};
static const char *const receive_usage[] = {
    "receive [-t milliseconds]", "Capture GDO0 pulse widths for 5 seconds:%s receive"
};
static const bp_command_def_t receive_def = {
    .name = "receive", .description = 0, .opts = receive_opts,
    .usage = receive_usage, .usage_count = count_of(receive_usage),
};

static const bp_command_opt_t sweep_opts[] = {
    { "start", 'a', BP_ARG_REQUIRED, "kHz", 0 },
    { "stop", 'z', BP_ARG_REQUIRED, "kHz", 0 },
    { "step", 's', BP_ARG_REQUIRED, "kHz", 0 },
    { "samples", 'n', BP_ARG_REQUIRED, "2-100", 0 },
    { "threshold", 'r', BP_ARG_REQUIRED, "positive dBm magnitude", 0 }, { 0 }
};
static const bp_command_def_t sweep_def = { .name = "sweep", .description = 0, .opts = sweep_opts };

static const bp_command_opt_t trace_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "100-10000 milliseconds", 0 }, { 0 }
};
static const bp_command_def_t trace_def = { .name = "trace", .description = 0, .opts = trace_opts };

static const bp_command_opt_t waterfall_opts[] = {
    { "start", 'a', BP_ARG_REQUIRED, "kHz", 0 },
    { "stop", 'z', BP_ARG_REQUIRED, "kHz", 0 },
    { "step", 's', BP_ARG_REQUIRED, "kHz", 0 },
    { "rows", 'n', BP_ARG_REQUIRED, "1-100", 0 }, { 0 }
};
static const bp_command_def_t waterfall_def = { .name = "waterfall", .description = 0, .opts = waterfall_opts };

static const bp_command_opt_t replay_opts[] = {
    { "repeat", 'r', BP_ARG_REQUIRED, "1-20", 0 }, { 0 }
};
static const bp_command_def_t replay_def = { .name = "replay", .description = 0, .opts = replay_opts };

static const bp_command_positional_t record_positionals[] = {
    { "file", "output .sub file", 0, true }, { 0 }
};
static const bp_command_opt_t record_opts[] = {
    { "time", 't', BP_ARG_REQUIRED, "milliseconds", 0 }, { 0 }
};
static const bp_command_def_t record_def = {
    .name = "record", .description = 0, .opts = record_opts,
    .positionals = record_positionals, .positional_count = 1,
};

static const bp_command_positional_t load_positionals[] = {
    { "file", "Flipper RAW .sub file", 0, false }, { 0 }
};
static const bp_command_opt_t load_opts[] = {
    { "repeat", 'r', BP_ARG_REQUIRED, "0-20 (0 loads only)", 0 }, { 0 }
};
static const char *const load_usage[] = {
    "load <file.sub> [-r repeats]",
    "Load without transmitting:%s load capture.sub",
    "Load and replay twice:%s load capture.sub -r 2"
};
static const bp_command_def_t load_def = {
    .name = "load", .description = 0, .opts = load_opts,
    .positionals = load_positionals, .positional_count = 1,
    .usage = load_usage, .usage_count = count_of(load_usage),
};

static const bp_command_positional_t send_positionals[] = {
    { "payload", "integer or hex value", 0, true }, { 0 }
};
static const bp_command_opt_t send_opts[] = {
    { "bits", 'b', BP_ARG_REQUIRED, "1-64", 0 },
    { "te", 't', BP_ARG_REQUIRED, "microseconds", 0 },
    { "repeat", 'r', BP_ARG_REQUIRED, "1-20", 0 }, { 0 }
};
static const char *const send_usage[] = {
    "send <payload> [-b bits] [-t TE_us] [-r repeats]",
    "Send a 24-bit Princeton frame:%s send 0x123456 -b 24 -t 350 -r 4"
};
static const bp_command_def_t send_def = {
    .name = "send", .description = 0, .opts = send_opts,
    .positionals = send_positionals, .positional_count = 1,
    .usage = send_usage, .usage_count = count_of(send_usage),
};

static bool command_ready(void) {
    if (mode_config.initialized) return true;
    printf("CC1101 is not initialized; check wiring and use 3.3V power.\r\n");
    return false;
}

static void pulse(bool level, uint32_t us);

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
    printf("RSSI: %d dBm\r\n", cc1101_rssi());
}

static void scan_handler(struct command_result *res) {
    if (bp_cmd_help_check(&scan_def, res->help_flag) || !command_ready()) return;
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
    bio_input(SUBGHZ_GDO0);
    if (!cc1101_rx()) {
        printf("Failed to enter RX state.\r\n");
        return false;
    }
    bool level = bio_get(SUBGHZ_GDO0);
    bool started = level; /* Flipper RAW data starts with a positive (high) duration. */
    absolute_time_t last = get_absolute_time(), until = make_timeout_time_ms(duration_ms);
    while (!time_reached(until) && capture_count < count_of(capture_buffer)) {
        bool now = bio_get(SUBGHZ_GDO0);
        if (now != level) {
            absolute_time_t stamp = get_absolute_time();
            int64_t us = absolute_time_diff_us(last, stamp);
            if (!started) {
                /* Discard the leading idle-low interval and begin at its rising edge. */
                started = now;
            } else if (us > 0) {
                capture_buffer[capture_count] = level ? (int32_t)us : -(int32_t)us;
                capture_count++;
            }
            level = now;
            last = stamp;
        }
        char c;
        if (rx_fifo_try_get(&c)) break;
        tight_loop_contents();
    }
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

static void receive_handler(struct command_result *res) {
    if (bp_cmd_help_check(&receive_def, res->help_flag) || !command_ready()) return;
    uint32_t duration = 5000, value;
    if (bp_cmd_get_uint32(&receive_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) { printf("Capture time must be 1-60000ms.\r\n"); return; }
    printf("Raw GDO0 pulse capture at %u.%03uMHz (H/L:microseconds; press a key to stop)\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
    capture_pulses(duration);
    for (uint32_t i = 0; i < capture_count; i++) {
        int32_t duration_us = capture_buffer[i];
        printf("%c:%lu%s", duration_us > 0 ? 'H' : 'L',
               (unsigned long)(duration_us > 0 ? duration_us : -duration_us),
               ((i + 1) % 8) ? "  " : "\r\n");
    }
    printf("\r\nCaptured %u pulses; ready for replay or record.\r\n", capture_count);
    analyze_capture();
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
    if (!cc1101_idle()) return false;
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
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

static void record_handler(struct command_result *res) {
    if (bp_cmd_help_check(&record_def, res->help_flag) || !command_ready()) return;
    char name[BP_FILENAME_MAX] = { 0 };
    if (!bp_cmd_get_positional_string(&record_def, 1, name, sizeof(name))) { bp_cmd_help_show(&record_def); return; }
    uint32_t duration = 5000, value;
    if (bp_cmd_get_uint32(&record_def, 't', &value)) duration = value;
    if (!duration || duration > 60000) { printf("Capture time must be 1-60000ms.\r\n"); return; }
    printf("Capturing up to 1024 pulses... press a key to stop.\r\n");
    if (!capture_pulses(duration)) { printf("No pulses captured.\r\n"); return; }
    analyze_capture();
    FIL file; FRESULT fr = f_open(&file, name, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) { printf("Unable to create %s (FatFs error %u).\r\n", name, fr); return; }
    bool write_ok = f_printf(&file, "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: %lu000\nPreset: FuriHalSubGhzPresetOok%luAsync\nProtocol: RAW\n",
                             (unsigned long)mode_config.frequency_khz,
                             (unsigned long)mode_config.bandwidth_khz) >= 0;
    for (uint32_t i = 0; i < capture_count; i++) {
        if ((i % 128) == 0 && f_puts("RAW_Data: ", &file) < 0) write_ok = false;
        if (f_printf(&file, "%ld%s", (long)capture_buffer[i],
                     ((i + 1) % 128 == 0 || i + 1 == capture_count) ? "\n" : " ") < 0) write_ok = false;
    }
    fr = f_close(&file);
    if (fr == FR_OK && write_ok) printf("Saved %u pulses to %s.\r\n", capture_count, name);
    else printf("Storage write failed for %s.\r\n", name);
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

    FIL file;
    FRESULT fr = f_open(&file, name, FA_READ);
    if (fr != FR_OK) { printf("Unable to open %s (FatFs error %u).\r\n", name, fr); return; }

    char line[256];
    uint32_t file_frequency_khz = 0;
    bool valid_filetype = false, valid_version = false;
    bool raw_continuation = false, raw_protocol = false;
    bool ook_preset = false, truncated = false;
    uint16_t preset_bandwidth_khz = 270;
    capture_count = 0;
    while (f_gets(line, sizeof(line), &file)) {
        bool complete_line = strchr(line, '\n') != NULL;
        char *data = NULL;
        if (!strncmp(line, "Filetype:", 9)) {
            valid_filetype = field_value_equals(line, 9, "Flipper SubGhz RAW File");
        } else if (!strncmp(line, "Version:", 8)) {
            valid_version = field_value_equals(line, 8, "1");
        } else if (!strncmp(line, "Frequency:", 10)) {
            if (!parse_frequency_field(line, &file_frequency_khz)) file_frequency_khz = 0;
        } else if (!strncmp(line, "Preset:", 7)) {
            bool preset_650 = field_value_equals(line, 7, "FuriHalSubGhzPresetOok650Async") ||
                              field_value_equals(line, 7, "Ook650Async") ||
                              field_value_equals(line, 7, "AM650");
            bool preset_270 = field_value_equals(line, 7, "FuriHalSubGhzPresetOok270Async") ||
                              field_value_equals(line, 7, "Ook270Async") ||
                              field_value_equals(line, 7, "AM270");
            ook_preset = preset_650 || preset_270;
            if (preset_650) preset_bandwidth_khz = 650;
            else if (preset_270) preset_bandwidth_khz = 270;
        } else if (!strncmp(line, "Protocol:", 9)) {
            raw_protocol = field_value_equals(line, 9, "RAW");
        } else if (!strncmp(line, "RAW_Data:", 9)) {
            data = line + 9;
            raw_continuation = !complete_line;
        } else if (raw_continuation) {
            data = line;
            raw_continuation = !complete_line;
        }

        if (data) {
            char *cursor = data;
            while (*cursor) {
                while (*cursor && *cursor != '-' && (*cursor < '0' || *cursor > '9')) cursor++;
                if (!*cursor) break;
                char *end = cursor;
                long duration = strtol(cursor, &end, 10);
                if (end == cursor) { cursor++; continue; }
                if (duration && duration >= -60000000l && duration <= 60000000l) {
                    if (capture_count < count_of(capture_buffer)) capture_buffer[capture_count++] = (int32_t)duration;
                    else truncated = true;
                }
                cursor = end;
            }
        }
    }
    fr = f_close(&file);
    if (fr != FR_OK) { capture_count = 0; printf("Error closing %s.\r\n", name); return; }
    if (!valid_filetype || !valid_version || !raw_protocol || !ook_preset ||
        !valid_frequency(file_frequency_khz) || !capture_count) {
        capture_count = 0;
        printf("Unsupported .sub file. A valid OOK RAW file with a supported frequency is required.\r\n");
        return;
    }

    uint32_t previous_frequency_khz = mode_config.frequency_khz;
    uint32_t previous_bandwidth_khz = mode_config.bandwidth_khz;
    if (!cc1101_set_ook_bandwidth(preset_bandwidth_khz) || !cc1101_set_frequency(file_frequency_khz)) {
        (void)cc1101_set_ook_bandwidth((uint16_t)previous_bandwidth_khz);
        (void)cc1101_set_frequency(previous_frequency_khz);
        (void)cc1101_rx();
        capture_count = 0;
        printf("Failed to apply the .sub radio preset.\r\n");
        return;
    }
    mode_config.frequency_khz = file_frequency_khz;
    mode_config.bandwidth_khz = preset_bandwidth_khz;
    if (!cc1101_rx()) {
        mode_config.frequency_khz = previous_frequency_khz;
        mode_config.bandwidth_khz = previous_bandwidth_khz;
        (void)cc1101_set_ook_bandwidth((uint16_t)previous_bandwidth_khz);
        (void)cc1101_set_frequency(previous_frequency_khz);
        (void)cc1101_rx();
        capture_count = 0;
        printf("Failed to enter RX state.\r\n");
        return;
    }
    printf("Loaded %u pulses from %s at %u.%03uMHz, OOK %ukHz%s.\r\n", capture_count, name,
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           preset_bandwidth_khz,
           truncated ? " (truncated to buffer capacity)" : "");
    analyze_capture();
    if (repeat) {
        printf("Replaying loaded capture %u time(s).\r\n", repeat);
        if (!transmit_capture(repeat)) printf("Replay failed: CC1101 state transition error.\r\n");
    }
}

static void pulse(bool level, uint32_t us) {
    bio_put(SUBGHZ_GDO0, level);
    if (us <= 10000u) busy_wait_us_32(us);
    else sleep_us(us);
}

static void send_handler(struct command_result *res) {
    if (bp_cmd_help_check(&send_def, res->help_flag) || !command_ready()) return;
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
    uint32_t bits = 24, te = 350, repeat = 4, value;
    if (bp_cmd_get_uint32(&send_def, 'b', &value)) bits = value;
    if (bp_cmd_get_uint32(&send_def, 't', &value)) te = value;
    if (bp_cmd_get_uint32(&send_def, 'r', &value)) repeat = value;
    if (!bits || bits > 64 || te < 50 || te > 2000 || !repeat || repeat > 20) {
        printf("Limits: bits 1-64, TE 50-2000us, repeats 1-20.\r\n"); return;
    }
    if (bits < 64 && (payload >> bits)) {
        printf("Payload does not fit in %u bits.\r\n", bits); return;
    }
    printf("Transmitting Princeton OOK at %u.%03uMHz, 0x%08lx%08lx/%u bits, TE=%uus, repeats=%u.\r\n",
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           (unsigned long)(payload >> 32), (unsigned long)payload, bits, te, repeat);
    if (!cc1101_idle()) { printf("Failed to enter IDLE state.\r\n"); return; }
    bio_output(SUBGHZ_GDO0);
    bio_put(SUBGHZ_GDO0, false);
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

const struct _mode_command_struct subghz_commands[] = {
    { .func = setfrequency_handler, .def = &frequency_def, .supress_fala_capture = true },
    { .func = rssi_handler, .def = &rssi_def, .supress_fala_capture = true },
    { .func = scan_handler, .def = &scan_def, .supress_fala_capture = true },
    { .func = sweep_handler, .def = &sweep_def, .supress_fala_capture = true },
    { .func = trace_handler, .def = &trace_def, .supress_fala_capture = true },
    { .func = waterfall_handler, .def = &waterfall_def, .supress_fala_capture = true },
    { .func = receive_handler, .def = &receive_def, .supress_fala_capture = true },
    { .func = replay_handler, .def = &replay_def, .supress_fala_capture = true },
    { .func = record_handler, .def = &record_def, .supress_fala_capture = true },
    { .func = load_handler, .def = &load_def, .supress_fala_capture = true },
    { .func = send_handler, .def = &send_def, .supress_fala_capture = true },
};
const uint32_t subghz_commands_count = count_of(subghz_commands);

uint32_t subghz_setup(void) {
    const char config_file[] = "bpsubghz.bp";
    const mode_config_t config[] = {
        { "$.frequency_khz", &mode_config.frequency_khz, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.gdo_pin", &mode_config.gdo_pin, MODE_CONFIG_FORMAT_DECIMAL },
        { "$.bandwidth_khz", &mode_config.bandwidth_khz, MODE_CONFIG_FORMAT_DECIMAL },
    };
    uint32_t frequency, gdo, bandwidth;
    bp_cmd_status_t status = bp_cmd_flag(&subghz_setup_def, 'f', &frequency);
    if (status == BP_CMD_INVALID) return 0;
    if (status == BP_CMD_MISSING) {
        if (!storage_load_mode(config_file, config, count_of(config)) ||
            !valid_frequency(mode_config.frequency_khz) || mode_config.gdo_pin > BIO3 ||
            (mode_config.bandwidth_khz != 270 && mode_config.bandwidth_khz != 650)) {
            mode_config.frequency_khz = SUBGHZ_DEFAULT_KHZ;
            mode_config.gdo_pin = BIO0;
            mode_config.bandwidth_khz = 270;
        }
        if (bp_cmd_prompt(&frequency_range, &frequency) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&gdo_range, &gdo) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&bandwidth_choice, &bandwidth) != BP_CMD_OK) return 0;
    } else {
        status = bp_cmd_flag(&subghz_setup_def, 'g', &gdo);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) gdo = BIO0;
        status = bp_cmd_flag(&subghz_setup_def, 'w', &bandwidth);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) bandwidth = 270;
    }
    if (!valid_frequency(frequency)) {
        printf("Supported bands: 300-348, 387-464, and 779-928 MHz.\r\n"); return 0;
    }
    if (gdo > BIO3) { printf("GDO0 must use BIO0, BIO1, BIO2, or BIO3.\r\n"); return 0; }
    mode_config.frequency_khz = frequency;
    mode_config.gdo_pin = gdo;
    mode_config.bandwidth_khz = bandwidth;
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
    if (!cc1101_set_ook_bandwidth((uint16_t)mode_config.bandwidth_khz)) {
        printf("\r\nInvalid OOK bandwidth.\r\n");
        subghz_cleanup();
        return 0;
    }
    if (!cc1101_rx()) {
        printf("\r\nFailed to enter RX state.\r\n");
        subghz_cleanup();
        return 0;
    }
    cc1101_present(&part, &version);
    printf("\r\nCC1101 detected (VERSION=0x%02x), tuned to %u.%03uMHz, OOK %ukHz.\r\n", version,
           mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000,
           mode_config.bandwidth_khz);
    return 1;
}

void subghz_cleanup(void) {
    if (mode_config.initialized) cc1101_deinit();
    mode_config.initialized = 0;
    const uint8_t pins[] = { SUBGHZ_GDO0, M_SPI_CDI, M_SPI_CS, M_SPI_CLK, M_SPI_CDO };
    for (uint i = 0; i < count_of(pins); i++) system_bio_update_purpose_and_label(false, pins[i], BP_PIN_MODE, 0);
}

void subghz_settings(void) {
    printf("CC1101 frequency: %u.%03u MHz\r\n", mode_config.frequency_khz / 1000, mode_config.frequency_khz % 1000);
    printf("CC1101 GDO0: BIO%u\r\n", mode_config.gdo_pin);
    printf("CC1101 OOK bandwidth: %ukHz\r\n", mode_config.bandwidth_khz);
}

void subghz_help(void) {
    printf("CC1101 Sub-GHz asynchronous OOK mode. Use a 3.3V CC1101 module.\r\n\r\n");
    printf("Connection: BIO6=SCK, BIO7=MOSI, BIO4=MISO, BIO5=CS, BIO%u=GDO0, GND=GND.\r\n",
           mode_config.gdo_pin);
    printf("Connect CC1101 VCC directly to 3.3V. Attach an antenna suitable for the selected band.\r\n");
    printf("Transmit only on frequencies and duty cycles permitted in your jurisdiction.\r\n\r\n");
    ui_help_mode_commands(subghz_commands, subghz_commands_count);
}

bool subghz_preflight_sanity_check(void) { return ui_help_sanity_check(true, 0x00); }
