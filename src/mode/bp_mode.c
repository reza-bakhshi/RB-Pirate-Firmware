/**
 * @file bp_mode.c
 * @brief Mode lifecycle manager and transactional switcher for Bus Pirate.
 */

#define _POSIX_C_SOURCE 200809L
#include "mode/bp_mode.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>

#include "system_config.h"
#include "kernel/bp_resource.h"
#include "kernel/bp_event.h"
#include "kernel/bp_kernel.h"
#include "modes.h"

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/bio.h"
#include "binmode/fala.h"
#include "ui/ui_term.h"
#else
// Host test stubs and hooks
void bio_init(void);
void fala_mode_change_hook(void);
const char* ui_term_color_error(void);
const char* ui_term_color_reset(void);
#endif

static bp_mode_t bp_mode_registry[MAXPROTO];
static bool bp_mode_registry_initialized = false;

bp_status_t bp_mode_init(void) {
    if (bp_mode_registry_initialized) {
        return BP_STATUS_OK;
    }

    memset(bp_mode_registry, 0, sizeof(bp_mode_registry));

    for (uint8_t i = 0; i < MAXPROTO; i++) {
        bp_mode_registry[i].mode_id = i;
        bp_mode_registry[i].name = modes[i].protocol_name;
        bp_mode_registry[i].commands = (const struct bp_command_def*)modes[i].setup_def;
        bp_mode_registry[i].command_count = (modes[i].mode_commands_count) ? *modes[i].mode_commands_count : 0;
        bp_mode_registry[i].is_active = (system_config.mode == i);
    }

    bp_mode_registry_initialized = true;
    return BP_STATUS_OK;
}

bp_status_t bp_mode_register(const bp_mode_t *mode) {
    if (!mode || mode->mode_id >= MAXPROTO) {
        return BP_STATUS_INVALID_PARAM;
    }
    if (!bp_mode_registry_initialized) {
        bp_mode_init();
    }
    bp_mode_registry[mode->mode_id] = *mode;
    return BP_STATUS_OK;
}

const bp_mode_t* bp_mode_get(uint8_t mode_id) {
    if (mode_id >= MAXPROTO) {
        return NULL;
    }
    if (!bp_mode_registry_initialized) {
        bp_mode_init();
    }
    return &bp_mode_registry[mode_id];
}

const bp_mode_t* bp_mode_get_by_name(const char *name) {
    if (!name) {
        return NULL;
    }
    if (!bp_mode_registry_initialized) {
        bp_mode_init();
    }
    for (uint8_t i = 0; i < MAXPROTO; i++) {
        if (bp_mode_registry[i].name && strcasecmp(bp_mode_registry[i].name, name) == 0) {
            return &bp_mode_registry[i];
        }
    }
    return NULL;
}

const bp_mode_t* bp_mode_get_active(void) {
    if (!bp_mode_registry_initialized) {
        bp_mode_init();
    }
    uint8_t m = system_config.mode;
    if (m >= MAXPROTO) {
        m = HIZ;
    }
    return &bp_mode_registry[m];
}

uint8_t bp_mode_get_active_id(void) {
    return system_config.mode;
}

bp_status_t bp_mode_switch(uint8_t new_mode) {
    if (new_mode >= MAXPROTO) {
        return BP_STATUS_INVALID_PARAM;
    }

    if (!bp_mode_registry_initialized) {
        bp_mode_init();
    }

    uint8_t current_mode = system_config.mode;

    // 1. Cleanup old mode BEFORE setup of target mode
    if (current_mode < MAXPROTO && modes[current_mode].protocol_cleanup) {
        modes[current_mode].protocol_cleanup();
    }

    // 2. Reclaim all peripheral resource leases owned by old mode
    bp_res_release_all(BP_RES_OWNER_MODE_BASE + current_mode);

    // 3. Make all BIO pins safe floating HiZ
    bio_init();

    // 4. Run target mode configuration / parameter prompts (Core 0 UI)
    if (modes[new_mode].protocol_setup && !modes[new_mode].protocol_setup()) {
        // User cancelled interactive prompt or bad args: rollback to HiZ
        system_config.mode = HIZ;
        if (modes[HIZ].protocol_setup_exc) {
            modes[HIZ].protocol_setup_exc();
        }
        for (uint8_t m = 0; m < MAXPROTO; m++) {
            bp_mode_registry[m].is_active = (m == HIZ);
        }
        fala_mode_change_hook();
        return BP_STATUS_CANCELLED;
    }

    // 5. Hardware activation of target mode
    system_config.mode = new_mode;
    if (modes[new_mode].protocol_setup_exc && !modes[new_mode].protocol_setup_exc()) {
        // Hardware activation failed! Transactional rollback to HiZ
        printf("\r\n%sFailed to setup mode %s, rolling back to HiZ%s\r\n",
               ui_term_color_error(), modes[new_mode].protocol_name, ui_term_color_reset());
        if (modes[new_mode].protocol_cleanup) {
            modes[new_mode].protocol_cleanup();
        }
        bp_res_release_all(BP_RES_OWNER_MODE_BASE + new_mode);
        bio_init();
        system_config.mode = HIZ;
        if (modes[HIZ].protocol_setup_exc) {
            modes[HIZ].protocol_setup_exc();
        }
        for (uint8_t m = 0; m < MAXPROTO; m++) {
            bp_mode_registry[m].is_active = (m == HIZ);
        }
        fala_mode_change_hook();
        return BP_STATUS_ERROR;
    }

    // 6. Target mode activated successfully
    for (uint8_t m = 0; m < MAXPROTO; m++) {
        bp_mode_registry[m].is_active = (m == new_mode);
    }

    fala_mode_change_hook();

    // Post mode change event to both cores
    bp_event_t ev = {
        .id = BP_EV_MODE_CHANGED,
        .source = 0,
#ifndef BP_HOST_TEST
        .timestamp_ms = to_ms_since_boot(get_absolute_time()),
#else
        .timestamp_ms = 0,
#endif
        .data.u32[0] = (uint32_t)current_mode,
        .data.u32[1] = (uint32_t)new_mode,
    };
    bp_kernel_post_event(0, &ev);
    bp_kernel_post_event(1, &ev);

    return BP_STATUS_OK;
}

bp_status_t bp_mode_to_hiz(void) {
    return bp_mode_switch(HIZ);
}

void bp_mode_periodic_service(uint32_t now_ms) {
    (void)now_ms;
    uint8_t m = system_config.mode;
    if (m < MAXPROTO && modes[m].protocol_periodic) {
        modes[m].protocol_periodic();
    }
}
