#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "commands/global/cmd_features.h"

typedef struct {
    const char* name;
    const char* origin;
    const char* capability;
    const char* status;
    const char* test_suite;
    const char* description;
} bp_feature_row_t;

static const bp_feature_row_t s_features[] = {
    {
        .name = "INFRARED",
        .origin = "ESP32-Bit-Pirate + MakeHex",
        .capability = "infrared",
        .status = "host-tested",
        .test_suite = "test_ir_protocols.sh",
        .description = "40+ protocols, 38kHz modulation, MakeHex engine, .ir file playback"
    },
    {
        .name = "SUBGHZ",
        .origin = "Flipper Zero + ESP32",
        .capability = "subghz",
        .status = "host-tested",
        .test_suite = "test_subghz.sh",
        .description = "CC1101 transceiver, 300-928MHz, Flipper .sub format, preset configs"
    },
    {
        .name = "RFID",
        .origin = "ESP32-Bit-Pirate + NXP",
        .capability = "rfid",
        .status = "host-tested",
        .test_suite = "test_rfid.sh",
        .description = "NXP PN532 HSU UART, ISO14443A/B, FeliCa, Jewel, MIFARE Classic/Ultralight"
    },
    {
        .name = "RF24",
        .origin = "ESP32-Bit-Pirate + RF24",
        .capability = "rf24",
        .status = "host-tested",
        .test_suite = "test_rf24.sh",
        .description = "nRF24L01+ 2.4GHz transceiver, SPI1 arbitration, 2Mbps/1Mbps/250kbps, carrier sweep"
    },
    {
        .name = "LCDTAP",
        .origin = "Shapoco LcdTap",
        .capability = "lcdtap",
        .status = "host-tested",
        .test_suite = "test_lcdtap.sh",
        .description = "Passive display sniffer, SPI4/SPI3/I2C, ST7789, SSD1306, ILI9341, ST7032, BMP export"
    },
    {
        .name = "CELL",
        .origin = "ESP32-Bit-Pirate",
        .capability = "cell",
        .status = "host-tested",
        .test_suite = "test_cell.sh",
        .description = "SIMCom AT command engine, cooperative cancellation (<100ms), SMS, USSD, calls"
    },
    {
        .name = "PS2",
        .origin = "Bus Pirate upstream + Dreg",
        .capability = "ps2-sniffer",
        .status = "host-tested",
        .test_suite = "test_sniffers.sh",
        .description = "Passive PS/2 keyboard/mouse monitor, 4 PIO programs, 0 leak teardown"
    },
    {
        .name = "USB",
        .origin = "Bus Pirate upstream + Dreg",
        .capability = "usb-sniffer",
        .status = "host-tested",
        .test_suite = "test_sniffers.sh",
        .description = "Full/Low-speed USB sniffer, 120MHz clock restore, keylogger, non-blocking trigger"
    },
    {
        .name = "BASE_MODES",
        .origin = "Bus Pirate upstream",
        .capability = "base-modes",
        .status = "ported",
        .test_suite = "test_mode.sh",
        .description = "HiZ, 1-Wire, UART, HDUART, I2C, SPI, 2-Wire, 3-Wire, DIO, LED, JTAG"
    },
    {
        .name = "BINARY_MODES",
        .origin = "Bus Pirate upstream",
        .capability = "binary-modes",
        .status = "host-tested",
        .test_suite = "test_bin_transport.sh",
        .description = "SUMP, BPIO, FALAIO, non-blocking CDC1 queues, zero cross-core calls"
    },
    {
        .name = "KERNEL",
        .origin = "RB-Pirate",
        .capability = "kernel",
        .status = "host-tested",
        .test_suite = "test_kernel.sh",
        .description = "Dual-core split, cooperative scheduler, seqlock UI snapshots, static memory model"
    },
};

static const uint32_t s_features_count = count_of(s_features);

static const char* const usage[] = {
    "features [-v|--verbose] [<feature_name>]",
    "List all ported features and verification status:%s features",
    "Show detailed feature properties and limits:%s features -v",
    "Inspect a specific feature:%s features subghz",
};

static const bp_command_opt_t features_opts[] = {
    { "verbose", 'v', BP_ARG_NONE, NULL, 0 },
    { 0 }
};

const bp_command_def_t cmd_features_def = {
    .name         = "features",
    .description  = T_CMDLN_FEATURES,
    .opts         = features_opts,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

static void print_single_feature(const bp_feature_row_t* f, bool verbose) {
    printf("%sFeature:%s     %s\r\n", ui_term_color_prompt(), ui_term_color_reset(), f->name);
    printf("%sCapability:%s  %s\r\n", ui_term_color_prompt(), ui_term_color_reset(), f->capability);
    printf("%sStatus:%s      %s%s%s\r\n",
           ui_term_color_prompt(), ui_term_color_reset(),
           strcmp(f->status, "verified") == 0 ? ui_term_color_prompt() : ui_term_color_info(),
           f->status, ui_term_color_reset());
    printf("%sSource:%s      %s\r\n", ui_term_color_prompt(), ui_term_color_reset(), f->origin);
    printf("%sTest Suite:%s  tests/%s\r\n", ui_term_color_prompt(), ui_term_color_reset(), f->test_suite);
    printf("%sDescription:%s %s\r\n", ui_term_color_prompt(), ui_term_color_reset(), f->description);
    if (verbose) {
        printf("%sChecklist:%s   openspec/checklists/%s.md\r\n",
               ui_term_color_prompt(), ui_term_color_reset(), f->capability);
    }
    printf("\r\n");
}

void cmd_features_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_features_def, res->help_flag)) {
        return;
    }

    bool verbose = bp_cmd_find_flag(&cmd_features_def, 'v');
    const char* filter = NULL;

    // Check positional argument
    char arg_buf[32];
    if (bp_cmd_get_positional_string(&cmd_features_def, 1, arg_buf, sizeof(arg_buf))) {
        filter = arg_buf;
    }

    if (filter && strlen(filter) > 0) {
        bool found = false;
        for (uint32_t i = 0; i < s_features_count; i++) {
            if (strcasecmp(filter, s_features[i].name) == 0 ||
                strcasecmp(filter, s_features[i].capability) == 0) {
                print_single_feature(&s_features[i], true);
                found = true;
                break;
            }
        }
        if (!found) {
            printf("%sError:%s Feature '%s' not found. Run 'features' to list all features.\r\n",
                   ui_term_color_error(), ui_term_color_reset(), filter);
        }
        return;
    }

    printf("\r\n%s=== RB-Pirate Feature Conformance & Port Status ===%s\r\n\r\n",
           ui_term_color_prompt(), ui_term_color_reset());

    printf("%-14s %-12s %-14s %-22s\r\n", "Feature", "Status", "Capability", "Host Test Suite");
    printf("%-14s %-12s %-14s %-22s\r\n", "-------", "------", "----------", "---------------");

    for (uint32_t i = 0; i < s_features_count; i++) {
        const bp_feature_row_t* f = &s_features[i];
        printf("%-14s %s%-12s%s %-14s tests/%-22s\r\n",
               f->name,
               strcmp(f->status, "verified") == 0 ? ui_term_color_prompt() : ui_term_color_info(),
               f->status,
               ui_term_color_reset(),
               f->capability,
               f->test_suite);

        if (verbose) {
            printf("  -> %s (%s)\r\n", f->description, f->origin);
        }
    }

    printf("\r\nHost tests do not verify electrical behavior or physical HIL.\r\n");
    printf("\r\nTotal Ported Features: %lu | Run all tests: 'tests/run_all.sh'\r\n\r\n",
           (unsigned long)s_features_count);
}
