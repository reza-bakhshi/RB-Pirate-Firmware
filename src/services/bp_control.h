/* Modified by RB-Pirate contributors, 2026. Core 0 control service boundary. */
#ifndef BP_CONTROL_H
#define BP_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*bp_control_poll_fn)(void);
typedef bool (*bp_control_request_fn)(uint8_t request);
/* Install during Core 0 startup. Hooks must not execute command parsing. */
void bp_control_init(bp_control_poll_fn poll, bp_control_request_fn request);
void bp_control_poll(void);
void bp_control_poll_core(uint8_t core);
bool bp_control_request(uint8_t request);
#endif
