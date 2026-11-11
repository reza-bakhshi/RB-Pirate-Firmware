/* Modified by RB-Pirate contributors, 2026. Reentrancy-safe control polling. */
#include "services/bp_control.h"
#include <stddef.h>
#if defined(BP_EMBEDDED)
#include "pico/stdlib.h"
#endif
static bp_control_poll_fn s_poll;
static bp_control_request_fn s_request;
static bool s_polling;
void bp_control_init(bp_control_poll_fn poll, bp_control_request_fn request) {
    s_poll = poll; s_request = request; s_polling = false;
}
void bp_control_poll_core(uint8_t core) {
#if defined(BP_EMBEDDED)
    if (get_core_num() != 0) return;
#endif
    if (core != 0 || s_polling || !s_poll) return;
    s_polling = true;
    s_poll();
    s_polling = false;
}
void bp_control_poll(void) {
#if defined(BP_EMBEDDED)
    bp_control_poll_core((uint8_t)get_core_num());
#else
    bp_control_poll_core(0);
#endif
}
bool bp_control_request(uint8_t request) {
#if defined(BP_EMBEDDED)
    if (get_core_num() != 0) return false;
#endif
    return s_request ? s_request(request) : false;
}
