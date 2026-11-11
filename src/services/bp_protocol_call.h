/* Modified by RB-Pirate contributors, 2026. Control-to-worker transaction calls. */
#ifndef BP_PROTOCOL_CALL_H
#define BP_PROTOCOL_CALL_H
#include "kernel/bp_job.h"
/* Core 0 submits a transaction, services control while waiting, and returns only
 * after Core 1 stops using ctx. Does not parse another command or binary packet.
 * Callbacks must return on cancellation; the watchdog covers a stuck worker. */
bp_status_t bp_protocol_call(const char *name, bp_job_slice_fn_t transaction, void *ctx);
#endif
