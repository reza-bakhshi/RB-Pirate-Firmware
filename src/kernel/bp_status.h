#ifndef BP_STATUS_H
#define BP_STATUS_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Unified Bus Pirate kernel status and error codes.
 */
typedef enum bp_status {
    BP_STATUS_OK               = 0,
    BP_STATUS_ERROR            = -1,
    BP_STATUS_TIMEOUT          = -2,
    BP_STATUS_CANCELLED        = -3,
    BP_STATUS_BUSY             = -4,
    BP_STATUS_INVALID_PARAM    = -5,
    BP_STATUS_QUEUE_FULL       = -6,
    BP_STATUS_QUEUE_EMPTY      = -7,
    BP_STATUS_NO_RESOURCE      = -8,
    BP_STATUS_NOT_SUPPORTED    = -9,
    BP_STATUS_DISCONNECTED     = -10,
    BP_STATUS_OVERFLOW         = -11,
    BP_STATUS_CORRUPTED        = -12,
} bp_status_t;

static inline bool bp_is_ok(bp_status_t status) {
    return status == BP_STATUS_OK;
}

static inline bool bp_is_error(bp_status_t status) {
    return status < 0;
}

static inline const char* bp_status_str(bp_status_t status) {
    switch (status) {
        case BP_STATUS_OK:            return "OK";
        case BP_STATUS_ERROR:         return "ERROR";
        case BP_STATUS_TIMEOUT:       return "TIMEOUT";
        case BP_STATUS_CANCELLED:     return "CANCELLED";
        case BP_STATUS_BUSY:          return "BUSY";
        case BP_STATUS_INVALID_PARAM: return "INVALID_PARAM";
        case BP_STATUS_QUEUE_FULL:    return "QUEUE_FULL";
        case BP_STATUS_QUEUE_EMPTY:   return "QUEUE_EMPTY";
        case BP_STATUS_NO_RESOURCE:   return "NO_RESOURCE";
        case BP_STATUS_NOT_SUPPORTED: return "NOT_SUPPORTED";
        case BP_STATUS_DISCONNECTED:  return "DISCONNECTED";
        case BP_STATUS_OVERFLOW:      return "OVERFLOW";
        case BP_STATUS_CORRUPTED:     return "CORRUPTED";
        default:                      return "UNKNOWN";
    }
}

#endif // BP_STATUS_H
