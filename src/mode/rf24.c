/**
 * @file rf24.c
 * @brief nRF24L01 2.4GHz packet and channel-analysis mode.
 *
 * Provides ordinary packet interoperability and passive RPD channel analysis.
 *
 * @author Bus Pirate Project
 * @date 2026
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "pirate/bio.h"
#include "pirate/nrf24l01.h"
#include "pirate/storage.h"
#include "ui/ui_help.h"
#include "ui/ui_term.h"
#include "lib/bp_args/bp_cmd.h"
#include "usb_rx.h"
#include "mode/rf24.h"

#define RF24_DEFAULT_CHANNEL 76u
#define RF24_DEFAULT_CE_PIN  BIO0

typedef struct {
    const char *name;
    const uint8_t *channels;
    uint32_t count;
} rf24_channel_group_t;

static const uint8_t wifi_channels[] = {
    6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
    22, 24, 26, 28,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
    46, 48, 50, 52,
    55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68
};
static const uint8_t ble_channels[] = { 1, 2, 3, 25, 26, 27, 79, 80, 81 };
static const uint8_t bluetooth_channels[] = {
    2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32,
    34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
    66, 68, 70, 72, 74, 76, 78, 80
};
static const uint8_t usb_channels[] = { 40, 50, 60 };
static const uint8_t video_channels[] = { 70, 75, 80 };
static const uint8_t rc_channels[] = { 1, 3, 5, 7 };

static const rf24_channel_group_t rf24_groups[] = {
    { "WiFi", wifi_channels, sizeof(wifi_channels) },
    { "BLE", ble_channels, sizeof(ble_channels) },
    { "Bluetooth", bluetooth_channels, sizeof(bluetooth_channels) },
    { "USB", usb_channels, sizeof(usb_channels) },
    { "Video", video_channels, sizeof(video_channels) },
    { "RC Toys", rc_channels, sizeof(rc_channels) },
};
#define RF24_GROUP_COUNT (sizeof(rf24_groups) / sizeof(rf24_groups[0]))

static struct {
    uint32_t channel;
    uint32_t ce_pin;
    uint32_t initialized;
} mode_config = { RF24_DEFAULT_CHANNEL, RF24_DEFAULT_CE_PIN, 0u };

static const char config_file[] = "bprf24.bp";
static const mode_config_t saved_config[] = {
    { "$.channel", &mode_config.channel, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.ce_pin", &mode_config.ce_pin, MODE_CONFIG_FORMAT_DECIMAL },
};

static const char pin_labels[][5] = { "CE", "MISO", "CSN", "SCK", "MOSI" };

static const bp_val_constraint_t channel_range = {
    .type = BP_VAL_UINT32, .u = { .min = 0, .max = 125, .def = RF24_DEFAULT_CHANNEL },
    .prompt = T_RF24_CHANNEL_MENU, .hint = T_RF24_CHANNEL_HINT,
};
static const bp_val_constraint_t ce_range = {
    .type = BP_VAL_UINT32, .u = { .min = BIO0, .max = BIO3, .def = RF24_DEFAULT_CE_PIN },
    .prompt = T_RF24_CE_MENU, .hint = T_RF24_CE_HINT,
};
static const bp_command_opt_t setup_opts[] = {
    { "channel", 'c', BP_ARG_REQUIRED, "0-125", T_RF24_ARG_CHANNEL, &channel_range },
    { "ce", 'e', BP_ARG_REQUIRED, "BIO0-BIO3", T_RF24_OPT_CE, &ce_range },
    { 0 },
};
const bp_command_def_t rf24_setup_def = {
    .name = "rf24", .description = T_RF24_MODE_DESCRIPTION, .opts = setup_opts,
};

static const bp_command_positional_t channel_positionals[] = {
    { "channel", "0-125", T_RF24_ARG_CHANNEL, true, &channel_range }, { 0 },
};
static const bp_command_def_t setchannel_def = {
    .name = "setchannel", .description = T_RF24_CMD_SETCHANNEL,
    .positionals = channel_positionals, .positional_count = 1,
};

static const bp_command_opt_t scan_opts[] = {
    { "samples", 'n', BP_ARG_REQUIRED, "1-100", T_RF24_OPT_SAMPLES },
    { "dwell", 'd', BP_ARG_REQUIRED, "140-10000us", T_RF24_OPT_DWELL },
    { 0 },
};
static const char *const scan_usage[] = {
    "scan [-n samples] [-d dwell_us]", "Scan all channels and select the strongest:%s scan -n 20",
};
static const bp_command_def_t scan_def = {
    .name = "scan", .description = T_RF24_CMD_SCAN, .opts = scan_opts,
    .usage = scan_usage, .usage_count = count_of(scan_usage),
};

static const bp_command_opt_t sweep_opts[] = {
    { "samples", 'n', BP_ARG_REQUIRED, "1-100", T_RF24_OPT_SAMPLES },
    { "dwell", 'd', BP_ARG_REQUIRED, "140-10000us", T_RF24_OPT_DWELL },
    { "threshold", 't', BP_ARG_REQUIRED, "0-100%", T_RF24_OPT_THRESHOLD },
    { 0 },
};
static const bp_command_def_t sweep_def = {
    .name = "sweep", .description = T_RF24_CMD_SWEEP, .opts = sweep_opts,
};

static const bp_command_opt_t waterfall_opts[] = {
    { "rows", 'r', BP_ARG_REQUIRED, "1-100", T_RF24_OPT_ROWS },
    { "samples", 'n', BP_ARG_REQUIRED, "1-20", T_RF24_OPT_SAMPLES },
    { "dwell", 'd', BP_ARG_REQUIRED, "140-10000us", T_RF24_OPT_DWELL },
    { 0 },
};
static const bp_command_def_t waterfall_def = {
    .name = "waterfall", .description = T_RF24_CMD_WATERFALL, .opts = waterfall_opts,
};

static const bp_val_choice_t rate_choices[] = {
    { "250k", "250", 0, NRF24_RATE_250KBPS },
    { "1m", "1000", 0, NRF24_RATE_1MBPS },
    { "2m", "2000", 0, NRF24_RATE_2MBPS },
};
static const bp_val_constraint_t rate_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = rate_choices, .count = count_of(rate_choices), .def = NRF24_RATE_1MBPS },
};
static const bp_val_choice_t crc_choices[] = {
    { "off", "0", 0, NRF24_CRC_OFF },
    { "8", NULL, 0, NRF24_CRC_8 },
    { "16", NULL, 0, NRF24_CRC_16 },
};
static const bp_val_constraint_t crc_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = crc_choices, .count = count_of(crc_choices), .def = NRF24_CRC_16 },
};
static const bp_command_positional_t send_positionals[] = {
    { "payload", "hex bytes, for example 0102aaff", T_RF24_ARG_PAYLOAD, true }, { 0 },
};
static const bp_command_opt_t send_opts[] = {
    { "address", 'a', BP_ARG_REQUIRED, "3-5 hex bytes", T_RF24_OPT_ADDRESS },
    { "channel", 'c', BP_ARG_REQUIRED, "0-125", T_RF24_ARG_CHANNEL, &channel_range },
    { "rate", 'r', BP_ARG_REQUIRED, "250k|1m|2m", T_RF24_OPT_RATE, &rate_choice },
    { "crc", 'x', BP_ARG_REQUIRED, "off|8|16", T_RF24_OPT_CRC, &crc_choice },
    { "fixed", 'f', BP_ARG_REQUIRED, "1-32", T_RF24_OPT_FIXED },
    { "no-ack", 'N', BP_ARG_NONE, NULL, T_RF24_OPT_NO_ACK },
    { "repeat", 'n', BP_ARG_REQUIRED, "1-100", T_RF24_OPT_REPEAT },
    { "delay", 'd', BP_ARG_REQUIRED, "1-60000ms", T_RF24_OPT_DELAY },
    { 0 },
};
static const char *const send_usage[] = {
    "send <hex_payload> [-a address] [-c channel] [-r rate] [-x crc] [-f length] [-N]",
    "Send four bytes with acknowledgement:%s send deadbeef -a e7e7e7e7e7",
};
static const bp_command_def_t send_def = {
    .name = "send", .description = T_RF24_CMD_SEND, .opts = send_opts,
    .positionals = send_positionals, .positional_count = 1,
    .usage = send_usage, .usage_count = count_of(send_usage),
};

static const bp_command_opt_t receive_opts[] = {
    { "address", 'a', BP_ARG_REQUIRED, "3-5 hex bytes", T_RF24_OPT_ADDRESS },
    { "channel", 'c', BP_ARG_REQUIRED, "0-125", T_RF24_ARG_CHANNEL, &channel_range },
    { "pipe", 'p', BP_ARG_REQUIRED, "0-5", T_RF24_OPT_PIPE },
    { "rate", 'r', BP_ARG_REQUIRED, "250k|1m|2m", T_RF24_OPT_RATE, &rate_choice },
    { "crc", 'x', BP_ARG_REQUIRED, "off|8|16", T_RF24_OPT_CRC, &crc_choice },
    { "fixed", 'f', BP_ARG_REQUIRED, "1-32", T_RF24_OPT_FIXED },
    { "no-ack", 'N', BP_ARG_NONE, NULL, T_RF24_OPT_NO_ACK },
    { "time", 't', BP_ARG_REQUIRED, "1-60000ms", T_RF24_OPT_TIME },
    { 0 },
};
static const char *const receive_usage[] = {
    "receive [-a address] [-c channel] [-p pipe] [-r rate] [-x crc] [-f length] [-N] [-t ms]",
    "Receive dynamic payloads for 10 seconds:%s receive -a e7e7e7e7e7 -t 10000",
};
static const bp_command_def_t receive_def = {
    .name = "receive", .description = T_RF24_CMD_RECEIVE, .opts = receive_opts,
    .usage = receive_usage, .usage_count = count_of(receive_usage),
};

static const bp_command_positional_t jam_channel_start_positionals[] = {
    { "start_channel", "0-125", T_RF24_ARG_CHANNEL, true, &channel_range }, { 0 },
};
static const bp_val_choice_t jam_group_choices[] = {
    { "wifi", NULL, 0, 0 },
    { "ble", NULL, 0, 1 },
    { "bluetooth", NULL, 0, 2 },
    { "usb", NULL, 0, 3 },
    { "video", NULL, 0, 4 },
    { "rc", NULL, 0, 5 },
    { "full", NULL, 0, 6 },
};
static const bp_val_constraint_t jam_group_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = jam_group_choices, .count = count_of(jam_group_choices) },
};
static const bp_command_opt_t jam_opts[] = {
    { "group", 'g', BP_ARG_REQUIRED, "wifi|ble|bluetooth|usb|video|rc|full", T_RF24_OPT_GROUP, &jam_group_choice },
    { "end", 'e', BP_ARG_REQUIRED, "0-125", T_RF24_OPT_END_CHANNEL, &channel_range },
    { "rate", 'r', BP_ARG_REQUIRED, "250k|1m|2m", T_RF24_OPT_RATE, &rate_choice },
    { 0 },
};
static const char *const jam_usage[] = {
    "jam [-g group] [-e end_channel] [-r rate]",
    "Jam WiFi channels:%s jam -g wifi",
    "Sweep-jam CH76-CH83:%s jam 76 -e 83",
    "Jam BLE channels at 2Mbps:%s jam -g ble -r 2m",
};
static const bp_command_def_t jam_def = {
    .name = "jam", .description = T_RF24_CMD_JAM, .opts = jam_opts,
    .positionals = jam_channel_start_positionals, .positional_count = 1,
    .usage = jam_usage, .usage_count = count_of(jam_usage),
};

static bool command_ready(void) {
    if (mode_config.initialized) {
        if (nrf24_present()) return true;
        nrf24_deinit();
        mode_config.initialized = 0u;
        printf("nRF24L01 stopped responding; checking power and wiring again.\r\n");
    }

    /* Mode entry disables the Bus Pirate PSU, so detection must happen only
     * after the user has enabled VOUT with W 3.3. */
    if (!ui_help_sanity_check(true, 0x00)) return false;

    mode_config.initialized = nrf24_init((uint8_t)mode_config.ce_pin);
    if (!mode_config.initialized || !nrf24_set_channel((uint8_t)mode_config.channel)) {
        if (mode_config.initialized) nrf24_deinit();
        mode_config.initialized = 0u;
        printf("nRF24L01 not detected. Verify: CE=BIO%u, CSN=BIO5, SCK=BIO6, "
               "MOSI=BIO7, MISO=BIO4, VCC=VOUT 3.3V, and GND=GND.\r\n",
               mode_config.ce_pin);
        return false;
    }

    printf("nRF24L01 detected on CH%u (%uMHz).\r\n", mode_config.channel,
           2400u + mode_config.channel);
    return true;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parse_hex_bytes(const char *text, uint8_t *data, uint8_t capacity, uint8_t *length) {
    uint8_t count = 0;
    int high = -1;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    while (*text) {
        int nibble = hex_nibble(*text++);
        if (nibble < 0) {
            char separator = text[-1];
            if (separator == ':' || separator == '-' || separator == '_' || separator == '.') continue;
            return false;
        }
        if (high < 0) high = nibble;
        else {
            if (count >= capacity) return false;
            data[count++] = (uint8_t)((high << 4) | nibble);
            high = -1;
        }
    }
    if (high >= 0 || !count) return false;
    *length = count;
    return true;
}

static void format_address(const uint8_t *address, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) printf("%02x", address[i]);
}

static bool parse_common_radio_options(const bp_command_def_t *def, nrf24_config_t *config) {
    static const uint8_t default_address[] = { 0xe7, 0xe7, 0xe7, 0xe7, 0xe7 };
    memset(config, 0, sizeof(*config));
    memcpy(config->address, default_address, sizeof(default_address));
    config->address_length = sizeof(default_address);
    config->channel = (uint8_t)mode_config.channel;
    config->pipe = 1u;
    config->data_rate = NRF24_RATE_1MBPS;
    config->crc = NRF24_CRC_16;
    config->dynamic_payload = true;
    config->payload_length = 32u;
    config->auto_ack = !bp_cmd_find_flag(def, 'N');

    uint32_t value;
    char address_text[24];
    if (bp_cmd_get_uint32(def, 'c', &value)) config->channel = (uint8_t)value;
    if (bp_cmd_get_uint32(def, 'p', &value)) config->pipe = (uint8_t)value;
    if (bp_cmd_get_uint32(def, 'r', &value)) config->data_rate = (nrf24_data_rate_t)value;
    if (bp_cmd_get_uint32(def, 'x', &value)) config->crc = (nrf24_crc_t)value;
    if (bp_cmd_get_uint32(def, 'f', &value)) {
        if (!value || value > 32u) {
            printf("Fixed payload length must be 1-32 bytes.\r\n");
            return false;
        }
        config->dynamic_payload = false;
        config->payload_length = (uint8_t)value;
    }
    if (config->channel > 125u || config->pipe > 5u) {
        printf("Channel must be 0-125 and pipe must be 0-5.\r\n");
        return false;
    }
    if (bp_cmd_get_string(def, 'a', address_text, sizeof(address_text))) {
        if (!parse_hex_bytes(address_text, config->address, sizeof(config->address), &config->address_length) ||
            config->address_length < 3u) {
            printf("Address must contain 3-5 hex bytes, for example e7e7e7e7e7.\r\n");
            return false;
        }
    }
    /* Enhanced ShockBurst acknowledgements require CRC. */
    if (config->crc == NRF24_CRC_OFF) config->auto_ack = false;
    return true;
}

static bool configure_rpd_receiver(void) {
    const nrf24_config_t config = {
        .channel = (uint8_t)mode_config.channel,
        .pipe = 0u,
        .address = { 0xe7, 0xe7, 0xe7 },
        .address_length = 3u,
        .data_rate = NRF24_RATE_1MBPS,
        .crc = NRF24_CRC_OFF,
        .dynamic_payload = false,
        .payload_length = 1u,
        .auto_ack = false,
    };
    if (!nrf24_configure_rx(&config)) {
        printf("Unable to configure nRF24L01 RPD receiver. Check power and wiring.\r\n");
        return false;
    }
    return true;
}

static bool tx_channel_allowed(const nrf24_config_t *config) {
    /* The radio hardware supports 2400-2525MHz, but the commonly available
     * 2.4GHz ISM allocation is 2400-2483.5MHz. Keep the occupied signal
     * inside that range: 2Mbps needs an extra channel of edge clearance. */
    if (config->data_rate == NRF24_RATE_2MBPS) {
        if (config->channel >= 1u && config->channel <= 82u) return true;
        printf("2Mbps transmission is limited to CH1-CH82 (2401-2482MHz) to stay inside "
               "the common 2.4GHz ISM band.\r\n");
        return false;
    }
    if (config->channel <= 83u) return true;
    printf("Transmission is limited to CH0-CH83 (2400-2483MHz), the common 2.4GHz ISM band.\r\n");
    return false;
}

static bool delay_cancelled(uint32_t delay_ms) {
    absolute_time_t until = make_timeout_time_ms(delay_ms);
    while (!time_reached(until)) {
        char c;
        if (rx_fifo_try_get(&c)) return true;
        sleep_ms(1u);
    }
    return false;
}

static bool scan_options(const bp_command_def_t *def, uint32_t *samples, uint32_t *dwell) {
    uint32_t value;
    *samples = 10u;
    *dwell = 160u;
    if (bp_cmd_get_uint32(def, 'n', &value)) *samples = value;
    if (bp_cmd_get_uint32(def, 'd', &value)) *dwell = value;
    if (!*samples || *samples > 100u || *dwell < 140u || *dwell > 10000u) {
        printf("Limits: samples 1-100, dwell 140-10000us.\r\n");
        return false;
    }
    return true;
}

static uint32_t channel_hits(uint8_t channel, uint32_t samples, uint32_t dwell) {
    uint32_t hits = 0;
    for (uint32_t sample = 0; sample < samples; sample++) {
        if (nrf24_rpd_sample(channel, dwell)) hits++;
    }
    return hits;
}

static void setchannel_handler(struct command_result *res) {
    if (bp_cmd_help_check(&setchannel_def, res->help_flag) || !command_ready()) return;
    uint32_t channel;
    if (!bp_cmd_get_positional_uint32(&setchannel_def, 1, &channel) || channel > 125u) {
        printf("Channel must be 0-125.\r\n");
        return;
    }
    if (!nrf24_set_channel((uint8_t)channel)) {
        printf("Unable to set RF channel.\r\n");
        return;
    }
    mode_config.channel = channel;
    storage_save_mode(config_file, saved_config, count_of(saved_config));
    printf("RF24 channel: %u (%uMHz)\r\n", channel, 2400u + channel);
}

static void scan_handler(struct command_result *res) {
    if (bp_cmd_help_check(&scan_def, res->help_flag) || !command_ready()) return;
    uint32_t samples, dwell;
    if (!scan_options(&scan_def, &samples, &dwell) || !configure_rpd_receiver()) return;
    uint32_t best_hits = 0;
    int best_channel = -1;
    printf("Scanning 2400-2525MHz using nRF24 RPD (press a key to stop)...\r\n");
    for (uint32_t channel = 0; channel <= 125u; channel++) {
        uint32_t hits = channel_hits((uint8_t)channel, samples, dwell);
        if (hits) {
            uint32_t activity = hits * 100u / samples;
            printf("CH%03u  %uMHz  %3u%%\r\n", channel, 2400u + channel, activity);
            if (hits > best_hits) {
                best_hits = hits;
                best_channel = (int)channel;
            }
        }
        char c;
        if (rx_fifo_try_get(&c)) break;
    }
    if (best_channel >= 0) {
        mode_config.channel = (uint32_t)best_channel;
        (void)nrf24_set_channel((uint8_t)best_channel);
        storage_save_mode(config_file, saved_config, count_of(saved_config));
        printf("Selected strongest activity: CH%u (%uMHz), %u%%.\r\n", mode_config.channel,
               2400u + mode_config.channel, best_hits * 100u / samples);
    } else {
        (void)nrf24_set_channel((uint8_t)mode_config.channel);
        printf("No activity detected; channel remains CH%u.\r\n", mode_config.channel);
    }
}

static void sweep_handler(struct command_result *res) {
    if (bp_cmd_help_check(&sweep_def, res->help_flag) || !command_ready()) return;
    uint32_t samples, dwell, threshold = 1u, value;
    if (!scan_options(&sweep_def, &samples, &dwell)) return;
    if (bp_cmd_get_uint32(&sweep_def, 't', &value)) threshold = value;
    if (threshold > 100u) {
        printf("Threshold must be 0-100 percent.\r\n");
        return;
    }
    if (!configure_rpd_receiver()) return;
    printf("Channel  Frequency  Activity\r\n");
    for (uint32_t channel = 0; channel <= 125u; channel++) {
        uint32_t hits = channel_hits((uint8_t)channel, samples, dwell);
        uint32_t activity = hits * 100u / samples;
        if (activity >= threshold) printf("CH%03u    %uMHz     %3u%%\r\n", channel, 2400u + channel, activity);
        char c;
        if (rx_fifo_try_get(&c)) break;
    }
    (void)nrf24_set_channel((uint8_t)mode_config.channel);
}

static char activity_pixel(uint32_t percent) {
    if (percent >= 75u) return '#';
    if (percent >= 50u) return 'O';
    if (percent >= 25u) return 'o';
    if (percent) return '.';
    return ' ';
}

static void waterfall_handler(struct command_result *res) {
    if (bp_cmd_help_check(&waterfall_def, res->help_flag) || !command_ready()) return;
    uint32_t rows = 10u, samples, dwell, value;
    if (!scan_options(&waterfall_def, &samples, &dwell)) return;
    if (samples > 20u) {
        printf("Waterfall samples must be 1-20.\r\n");
        return;
    }
    if (bp_cmd_get_uint32(&waterfall_def, 'r', &value)) rows = value;
    if (!rows || rows > 100u) {
        printf("Rows must be 1-100.\r\n");
        return;
    }
    if (!configure_rpd_receiver()) return;
    printf("2400MHz | RPD activity (# strongest, then O o .) | 2525MHz\r\n");
    for (uint32_t row = 0; row < rows; row++) {
        printf("%03u|", row);
        for (uint32_t channel = 0; channel <= 125u; channel++) {
            uint32_t hits = channel_hits((uint8_t)channel, samples, dwell);
            putchar(activity_pixel(hits * 100u / samples));
        }
        printf("|\r\n");
        char c;
        if (rx_fifo_try_get(&c)) break;
    }
    (void)nrf24_set_channel((uint8_t)mode_config.channel);
}

static void send_handler(struct command_result *res) {
    if (bp_cmd_help_check(&send_def, res->help_flag) || !command_ready()) return;
    char payload_text[96];
    uint8_t payload[32], payload_length;
    if (!bp_cmd_get_positional_string(&send_def, 1, payload_text, sizeof(payload_text)) ||
        !parse_hex_bytes(payload_text, payload, sizeof(payload), &payload_length)) {
        printf("Payload must contain 1-32 hex bytes, for example deadbeef.\r\n");
        return;
    }
    nrf24_config_t config;
    if (!parse_common_radio_options(&send_def, &config)) return;
    if (!tx_channel_allowed(&config)) return;
    if (!config.dynamic_payload && payload_length > config.payload_length) {
        printf("Payload is longer than the configured fixed payload length.\r\n");
        return;
    }
    uint32_t repeat = 1u, delay_ms = 1000u, value;
    if (bp_cmd_get_uint32(&send_def, 'n', &value)) repeat = value;
    if (bp_cmd_get_uint32(&send_def, 'd', &value)) delay_ms = value;
    if (!repeat || repeat > 100u || !delay_ms || delay_ms > 60000u) {
        printf("Limits: repeat 1-100 and delay 1-60000ms.\r\n");
        return;
    }
    if (!nrf24_configure_tx(&config)) {
        printf("Unable to configure nRF24L01 transmitter.\r\n");
        return;
    }
    printf("TX CH%u %uMHz, address ", config.channel, 2400u + config.channel);
    format_address(config.address, config.address_length);
    printf(", %u byte(s), %s.\r\n", payload_length, config.auto_ack ? "ACK enabled" : "no ACK");
    for (uint32_t count = 0; count < repeat; count++) {
        bool acknowledged = false;
        bool completed = nrf24_send(payload, payload_length, &acknowledged);
        printf("TX #%u: %s\r\n", count + 1u,
               !completed ? "timeout" : config.auto_ack ? (acknowledged ? "ACK" : "no ACK") : "sent");
        if (count + 1u < repeat && delay_cancelled(delay_ms)) break;
    }
}

static void print_payload(const uint8_t *payload, uint8_t length) {
    printf("HEX:");
    for (uint8_t i = 0; i < length; i++) printf(" %02x", payload[i]);
    printf("  ASCII: ");
    for (uint8_t i = 0; i < length; i++) putchar(isprint(payload[i]) ? payload[i] : '.');
    printf("\r\n");
}

static void receive_handler(struct command_result *res) {
    if (bp_cmd_help_check(&receive_def, res->help_flag) || !command_ready()) return;
    nrf24_config_t config;
    if (!parse_common_radio_options(&receive_def, &config)) return;
    uint32_t duration_ms = 5000u, value;
    if (bp_cmd_get_uint32(&receive_def, 't', &value)) duration_ms = value;
    if (!duration_ms || duration_ms > 60000u) {
        printf("Receive time must be 1-60000ms.\r\n");
        return;
    }
    if (!nrf24_configure_rx(&config) || !nrf24_start_listening()) {
        printf("Unable to configure nRF24L01 receiver.\r\n");
        return;
    }
    printf("RX CH%u %uMHz pipe %u, address ", config.channel, 2400u + config.channel, config.pipe);
    format_address(config.address, config.address_length);
    printf(" for %ums (press a key to stop).\r\n", duration_ms);
    uint32_t packet_count = 0;
    absolute_time_t until = make_timeout_time_ms(duration_ms);
    while (!time_reached(until)) {
        uint8_t pipe;
        if (nrf24_available(&pipe)) {
            uint8_t payload[32], length;
            if (nrf24_receive(payload, sizeof(payload), &length)) {
                printf("RX #%u pipe=%u len=%u  ", ++packet_count, pipe, length);
                print_payload(payload, length);
            }
        }
        char c;
        if (rx_fifo_try_get(&c)) break;
        tight_loop_contents();
    }
    nrf24_stop_listening();
    nrf24_flush_rx();
    printf("Receive stopped; %u packet(s).\r\n", packet_count);
}

static void jam_handler(struct command_result *res) {
    if (bp_cmd_help_check(&jam_def, res->help_flag) || !command_ready()) return;
    uint32_t value;
    nrf24_data_rate_t rate = NRF24_RATE_2MBPS;
    if (bp_cmd_get_uint32(&jam_def, 'r', &value)) rate = (nrf24_data_rate_t)value;
    const uint8_t *channels = NULL;
    uint32_t channel_count = 0;
    uint32_t group_idx;
    if (bp_cmd_get_uint32(&jam_def, 'g', &value)) {
        if (value >= RF24_GROUP_COUNT) {
            printf("Invalid group index.\r\n");
            return;
        }
        channels = rf24_groups[value].channels;
        channel_count = rf24_groups[value].count;
        printf("Jamming group: %s (%u channels)\r\n", rf24_groups[value].name, channel_count);
    } else {
        uint32_t start_channel;
        if (bp_cmd_get_positional_uint32(&jam_def, 1, &start_channel)) {
            if (start_channel > 125u) {
                printf("Start channel must be 0-125.\r\n");
                return;
            }
            uint32_t end_channel = start_channel;
            if (bp_cmd_get_uint32(&jam_def, 'e', &value)) end_channel = value;
            if (end_channel > 125u || end_channel < start_channel) {
                printf("End channel must be %u-125 and >= start channel.\r\n", start_channel);
                return;
            }
            static uint8_t range_channels[126];
            channel_count = end_channel - start_channel + 1u;
            for (uint32_t i = 0; i < channel_count; i++) range_channels[i] = (uint8_t)(start_channel + i);
            channels = range_channels;
            printf("Jamming CH%u-CH%u (%u-%uMHz)\r\n", start_channel, end_channel,
                   2400u + start_channel, 2400u + end_channel);
        } else {
            static uint8_t full_channels[126];
            for (uint32_t i = 0; i <= 125u; i++) full_channels[i] = (uint8_t)i;
            channels = full_channels;
            channel_count = 126u;
            printf("Jamming full range: CH0-CH125 (2400-2525MHz)\r\n");
        }
    }
    printf("*** WARNING: RF jamming may be illegal in your jurisdiction. ***\r\n");
    printf("Rate: %s. Press any key to stop.\r\n", rate == NRF24_RATE_2MBPS ? "2Mbps" : rate == NRF24_RATE_1MBPS ? "1Mbps" : "250kbps");
    if (!nrf24_jam_start((uint8_t)rate)) {
        printf("Failed to start jam mode.\r\n");
        return;
    }
    while (true) {
        for (uint32_t i = 0; i < channel_count; i++) {
            nrf24_jam_channel(channels[i]);
            char c;
            if (rx_fifo_try_get(&c)) {
                nrf24_tx_stop();
                nrf24_deinit();
                mode_config.initialized = 0u;
                printf("Jam stopped by user.\r\n");
                return;
            }
        }
    }
}

const struct _mode_command_struct rf24_commands[] = {
    { .func = setchannel_handler, .def = &setchannel_def, .supress_fala_capture = true },
    { .func = scan_handler, .def = &scan_def, .supress_fala_capture = true },
    { .func = sweep_handler, .def = &sweep_def, .supress_fala_capture = true },
    { .func = waterfall_handler, .def = &waterfall_def, .supress_fala_capture = true },
    { .func = send_handler, .def = &send_def, .supress_fala_capture = true },
    { .func = receive_handler, .def = &receive_def, .supress_fala_capture = true },
    { .func = jam_handler, .def = &jam_def, .supress_fala_capture = true },
};
const uint32_t rf24_commands_count = count_of(rf24_commands);

uint32_t rf24_setup(void) {
    uint32_t channel, ce_pin;
    bp_cmd_status_t status = bp_cmd_flag(&rf24_setup_def, 'c', &channel);
    if (status == BP_CMD_INVALID) return 0;
    if (status == BP_CMD_MISSING) {
        bool saved_valid = storage_load_mode(config_file, saved_config, count_of(saved_config)) &&
                           mode_config.channel <= 125u && mode_config.ce_pin <= BIO3;
        if (saved_valid) {
            printf("\r\n\r\n%s%s%s\r\n", ui_term_color_info(), GET_T(T_USE_PREVIOUS_SETTINGS),
                   ui_term_color_reset());
            rf24_settings();
            bp_yn_result_t previous = bp_cmd_yes_no_exit("");
            if (previous == BP_YN_EXIT) return 0;
            if (previous == BP_YN_YES) return 1;
        } else {
            mode_config.channel = RF24_DEFAULT_CHANNEL;
            mode_config.ce_pin = RF24_DEFAULT_CE_PIN;
        }
        if (bp_cmd_prompt(&channel_range, &channel) != BP_CMD_OK) return 0;
        if (bp_cmd_prompt(&ce_range, &ce_pin) != BP_CMD_OK) return 0;
    } else {
        status = bp_cmd_flag(&rf24_setup_def, 'e', &ce_pin);
        if (status == BP_CMD_INVALID) return 0;
        if (status == BP_CMD_MISSING) ce_pin = RF24_DEFAULT_CE_PIN;
    }
    if (channel > 125u || ce_pin > BIO3) return 0;
    mode_config.channel = channel;
    mode_config.ce_pin = ce_pin;
    storage_save_mode(config_file, saved_config, count_of(saved_config));
    return 1;
}

uint32_t rf24_setup_exc(void) {
    system_bio_update_purpose_and_label(true, (uint8_t)mode_config.ce_pin, BP_PIN_MODE, pin_labels[0]);
    system_bio_update_purpose_and_label(true, M_SPI_CDI, BP_PIN_MODE, pin_labels[1]);
    system_bio_update_purpose_and_label(true, M_SPI_CS, BP_PIN_MODE, pin_labels[2]);
    system_bio_update_purpose_and_label(true, M_SPI_CLK, BP_PIN_MODE, pin_labels[3]);
    system_bio_update_purpose_and_label(true, M_SPI_CDO, BP_PIN_MODE, pin_labels[4]);
    mode_config.initialized = 0u;

    /* ui_mode_enable_args() disables the PSU before calling setup_exc(), so
     * print the wiring and enter the mode before trying to contact the radio. */
    printf("\r\nRF24 connections:\r\n");
    printf("  CE   -> BIO%u\r\n", mode_config.ce_pin);
    printf("  CSN  -> BIO5\r\n");
    printf("  SCK  -> BIO6\r\n");
    printf("  MOSI -> BIO7\r\n");
    printf("  MISO -> BIO4\r\n");
    printf("  VCC  -> VOUT (3.3V only)\r\n");
    printf("  GND  -> GND\r\n\r\n");
    printf("Enable module power with W 3.3, then run an RF24 command to detect the module.\r\n");
    printf("A 10uF capacitor close to the module is recommended.\r\n");
    return 1;
}

void rf24_cleanup(void) {
    if (mode_config.initialized) {
        nrf24_power_down();
        nrf24_deinit();
    }
    mode_config.initialized = 0u;
    const uint8_t pins[] = { (uint8_t)mode_config.ce_pin, M_SPI_CDI, M_SPI_CS, M_SPI_CLK, M_SPI_CDO };
    for (uint i = 0; i < count_of(pins); i++) {
        system_bio_update_purpose_and_label(false, pins[i], BP_PIN_MODE, 0);
    }
}

void rf24_settings(void) {
    printf("nRF24L01 channel: %u (%uMHz)\r\n", mode_config.channel, 2400u + mode_config.channel);
    printf("nRF24L01 CE: BIO%u\r\n", mode_config.ce_pin);
}

void rf24_help(void) {
    printf("nRF24L01 2.4GHz packet and passive RPD analysis mode. Use a 3.3V module.\r\n\r\n");
    printf("Connection: BIO6=SCK, BIO7=MOSI, BIO4=MISO, BIO5=CSN, BIO%u=CE, GND=GND.\r\n",
           mode_config.ce_pin);
    printf("Connect VCC to VOUT, then use W 3.3; a local 10uF capacitor is recommended.\r\n");
    printf("Passive analysis covers CH0-CH125; transmission is restricted to the common "
           "2400-2483.5MHz ISM band.\r\n");
    printf("Transmit only to equipment you control and within local 2.4GHz ISM regulations.\r\n\r\n");
    ui_help_mode_commands(rf24_commands, rf24_commands_count);
}
