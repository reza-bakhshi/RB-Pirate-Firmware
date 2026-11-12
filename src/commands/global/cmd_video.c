/**
 * @file cmd_video.c
 * @brief Command handler for RP2350 HSTX DVI digital video output.
 */

#include "commands/global/cmd_video.h"
#include <stdio.h>
#include <string.h>
#include "pirate.h"
#include "pirate/hstx_dvi.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"

enum video_actions {
    VIDEO_ACTION_STATUS = 1,
    VIDEO_ACTION_ON,
    VIDEO_ACTION_OFF,
    VIDEO_ACTION_CLS,
    VIDEO_ACTION_TEST,
};

static const char* const usage[] = {
    "video [on|off|cls|test|status]",
    "Show external video generator status:%s video",
    "Enable 640x480 DVI video output:%s video on",
    "Disable video generation:%s video off",
    "Clear external video screen:%s video cls",
    "Render color test pattern:%s video test",
};

static const bp_command_action_t video_actions[] = {
    { VIDEO_ACTION_ON,     "on",     0 },
    { VIDEO_ACTION_OFF,    "off",    0 },
    { VIDEO_ACTION_CLS,    "cls",    0 },
    { VIDEO_ACTION_TEST,   "test",   0 },
    { VIDEO_ACTION_STATUS, "status", 0 },
};

const bp_command_def_t cmd_video_def = {
    .name             = "video",
    .description      = 0,
    .actions          = video_actions,
    .action_count     = count_of(video_actions),
    .opts             = NULL,
    .positionals      = NULL,
    .positional_count = 0,
    .usage            = usage,
    .usage_count      = count_of(usage),
};

void cmd_video_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_video_def, res->help_flag)) {
        return;
    }

    if (!hstx_dvi_is_supported()) {
        printf("%sNotice:%s DVI output is unavailable: this board has no supported DVI wiring.\r\n",
               ui_term_color_warning(), ui_term_color_reset());
        res->error = true;
        return;
    }

    uint32_t action = VIDEO_ACTION_STATUS;
    bp_cmd_get_action(&cmd_video_def, &action);

    switch (action) {
        case VIDEO_ACTION_ON:
            hstx_dvi_enable(true);
            printf("%sRP2350 HSTX DVI video generator enabled.%s (640x480 @ 60Hz DVI)\r\n",
                   ui_term_color_info(), ui_term_color_reset());
            printf("Mirroring VT100 interactive console to external monitor.\r\n");
            break;

        case VIDEO_ACTION_OFF:
            hstx_dvi_enable(false);
            printf("%sRP2350 HSTX DVI video generator disabled.%s\r\n",
                   ui_term_color_info(), ui_term_color_reset());
            break;

        case VIDEO_ACTION_CLS:
            hstx_dvi_clear();
            printf("%sExternal video screen cleared.%s\r\n",
                   ui_term_color_info(), ui_term_color_reset());
            break;

        case VIDEO_ACTION_TEST:
            if (!hstx_dvi_is_enabled()) {
                hstx_dvi_enable(true);
            }
            hstx_dvi_test_pattern();
            printf("%sRendered 16-color test pattern and diagnostic banner to external display.%s\r\n",
                   ui_term_color_info(), ui_term_color_reset());
            break;

        case VIDEO_ACTION_STATUS:
        default:
            printf("\r\n%sRP2350 HSTX DVI Video Generator Status:%s\r\n",
                   ui_term_color_info(), ui_term_color_reset());
            printf("  Hardware Support: %s\r\n", hstx_dvi_is_supported() ? "Available (RP2350 HSTX Serializer)" : "Unavailable");
            printf("  Video Output:     %s\r\n", hstx_dvi_is_enabled() ? "Active (Transmitting DVI-D 640x480@60Hz)" : "Disabled");
            printf("  Resolution:       640 x 480 pixels\r\n");
            printf("  Text Grid:        80 columns x 30 rows (8x16 Monospace)\r\n");
            printf("  Scanline Buffers: 2 x 640 bytes (Ping-Pong DMA)\r\n");
            printf("  RAM Footprint:    4.9 KB static (Zero Heap Allocation)\r\n\r\n");
            break;
    }
}
