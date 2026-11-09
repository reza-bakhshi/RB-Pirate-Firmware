/**
 * @file bp_protocol.h
 * @brief Protocol data plane state tracker, heartbeats, and recovery for Core 1.
 */

#ifndef BP_PROTOCOL_H
#define BP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bp_proto_state {
    BP_PROTO_STATE_IDLE    = 0,
    BP_PROTO_STATE_ACTIVE  = 1,
    BP_PROTO_STATE_FAULT   = 2,
} bp_proto_state_t;

typedef struct bp_protocol_telemetry {
    volatile uint32_t heartbeat_ticks;
    volatile uint32_t last_heartbeat_ms;
    volatile uint8_t  state;        /* bp_proto_state_t */
    volatile uint8_t  active_mode;  /* Mode ID */
    volatile uint32_t active_job_id;
    uint32_t          transitions;
    uint32_t          faults;
    uint32_t          recoveries;
} bp_protocol_telemetry_t;

/**
 * @brief Initialize protocol data plane telemetry and state.
 */
bp_status_t bp_protocol_init(void);

/**
 * @brief Heartbeat tick called by Core 1 protocol engines during execution.
 */
void bp_protocol_heartbeat(void);

/**
 * @brief Service function invoked in Core 1 kernel loop.
 */
void bp_protocol_service(uint32_t now_ms);

/**
 * @brief Query current protocol engine execution state.
 */
bp_proto_state_t bp_protocol_get_state(void);

/**
 * @brief Set current protocol engine execution state.
 */
void bp_protocol_set_state(bp_proto_state_t state, uint32_t job_id);

/**
 * @brief Check protocol engine health (called from Core 0 supervisory monitor).
 * Returns false if active protocol has stalled beyond max_stall_ms.
 */
bool bp_protocol_is_healthy(uint32_t now_ms, uint32_t max_stall_ms);

/**
 * @brief Perform safe recovery when protocol stall or fault is detected.
 */
bp_status_t bp_protocol_recover(void);

/**
 * @brief Get copy of protocol data plane telemetry.
 */
void bp_protocol_get_telemetry(bp_protocol_telemetry_t *out_telem);

#ifdef __cplusplus
}
#endif

#endif // BP_PROTOCOL_H
