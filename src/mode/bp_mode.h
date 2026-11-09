/**
 * @file bp_mode.h
 * @brief Mode lifecycle interface and compatibility adapter for Bus Pirate.
 *
 * Defines the unified bp_mode_t descriptor, transactional mode switching with
 * lease reclamation and automatic rollback to HiZ on setup failure.
 */

#ifndef BP_MODE_H
#define BP_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/bp_status.h"
#include "command_struct.h"
#include "modes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bp_mode {
    uint8_t mode_id;
    const char *name;
    bp_status_t (*probe)(void);
    bp_status_t (*configure)(void);   /* Interactive prompts / CLI arguments (Core 0) */
    bp_status_t (*start)(void);       /* Hardware initialization (Core 1 / Data plane) */
    void (*stop)(void);               /* Hardware teardown */
    void (*deinit)(void);             /* Lease release */
    void (*periodic)(uint32_t now_ms);/* Async data polling */
    const struct bp_command_def *commands;
    uint16_t command_count;
    bool is_active;
} bp_mode_t;

/**
 * @brief Initialize mode lifecycle manager and register all compatibility adapters.
 */
bp_status_t bp_mode_init(void);

/**
 * @brief Register a mode descriptor into the mode registry.
 */
bp_status_t bp_mode_register(const bp_mode_t *mode);

/**
 * @brief Get mode descriptor by mode index (0..MAXPROTO-1).
 */
const bp_mode_t* bp_mode_get(uint8_t mode_id);

/**
 * @brief Get mode descriptor by protocol name (case-insensitive).
 */
const bp_mode_t* bp_mode_get_by_name(const char *name);

/**
 * @brief Get active mode descriptor.
 */
const bp_mode_t* bp_mode_get_active(void);

/**
 * @brief Get active mode ID.
 */
uint8_t bp_mode_get_active_id(void);

/**
 * @brief Transactionally switch mode with cleanup-before-setup, lease reclamation,
 * pin safety, and automatic rollback to HiZ on failure.
 *
 * @param new_mode Target mode index (0..MAXPROTO-1).
 * @return BP_STATUS_OK on success, error code on failure.
 */
bp_status_t bp_mode_switch(uint8_t new_mode);

/**
 * @brief Force switch back to safe HiZ mode.
 */
bp_status_t bp_mode_to_hiz(void);

/**
 * @brief Execute periodic service step of the active mode (poll async data).
 */
void bp_mode_periodic_service(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif // BP_MODE_H
