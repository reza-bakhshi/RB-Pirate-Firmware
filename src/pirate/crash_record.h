#ifndef PIRATE_CRASH_RECORD_H
#define PIRATE_CRASH_RECORD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BP_CRASH_MAGIC   0x42504352u /* "BPCR" */
#define BP_CRASH_VERSION 1u

typedef enum {
    BP_CRASH_REASON_NONE = 0,
    BP_CRASH_REASON_WATCHDOG = 1,
    BP_CRASH_REASON_HARDFAULT = 2,
    BP_CRASH_REASON_ASSERT = 3,
    BP_CRASH_REASON_PANIC = 4,
    BP_CRASH_REASON_FORCED = 5,
} bp_crash_reason_t;

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t sp;
} bp_crash_registers_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t reason;
    uint32_t core_id;
    uint32_t uptime_ms;
    uint32_t consecutive_crashes;
    bp_crash_registers_t regs;
    char file[64];
    uint32_t line;
    char message[80];
    uint32_t crc32;
} bp_crash_record_t;

#ifdef __cplusplus
extern "C" {
#endif

extern bp_crash_record_t bp_crash_record;

uint32_t bp_crash_calc_crc32(const bp_crash_record_t* rec);
bool bp_crash_record_is_valid(const bp_crash_record_t* rec);
void bp_crash_record_init(void);
void bp_crash_record_capture(bp_crash_reason_t reason,
                             uint32_t core_id,
                             const char* file,
                             uint32_t line,
                             const char* msg,
                             const bp_crash_registers_t* regs);
void bp_crash_record_clear(void);
void bp_crash_record_reset_consecutive(void);
void bp_crash_report_print(void);
const char* bp_crash_reason_str(bp_crash_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif // PIRATE_CRASH_RECORD_H
