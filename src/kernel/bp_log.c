#include "kernel/bp_log.h"
#include <stdio.h>
#include <string.h>

static bp_log_ring_t s_log_ring;

bp_status_t bp_log_subsystem_init(void) {
    s_log_ring.head = 0;
    s_log_ring.tail = 0;
    s_log_ring.drops = 0;
    memset(s_log_ring.entries, 0, sizeof(s_log_ring.entries));
    return BP_STATUS_OK;
}

bp_status_t bp_log_post(bp_log_level_t level, const char* tag, const char* msg, uint8_t core, uint32_t now_ms) {
    if (!msg) return BP_STATUS_INVALID_PARAM;

    uint32_t head = s_log_ring.head;
    uint32_t tail = s_log_ring.tail;

    if ((head - tail) >= BP_LOG_RING_CAPACITY) {
        s_log_ring.drops++;
        return BP_STATUS_QUEUE_FULL;
    }

    bp_log_entry_t* entry = &s_log_ring.entries[head & (BP_LOG_RING_CAPACITY - 1)];
    entry->timestamp_ms = now_ms;
    entry->level = (uint8_t)level;
    entry->core = core;

    if (tag) {
        strncpy(entry->tag, tag, BP_LOG_TAG_MAX - 1);
        entry->tag[BP_LOG_TAG_MAX - 1] = '\0';
    } else {
        entry->tag[0] = '\0';
    }

    strncpy(entry->msg, msg, BP_LOG_MSG_MAX - 1);
    entry->msg[BP_LOG_MSG_MAX - 1] = '\0';

    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_log_ring.head = head + 1;

    return BP_STATUS_OK;
}

bp_status_t bp_log_pop(bp_log_entry_t* entry) {
    if (!entry) return BP_STATUS_INVALID_PARAM;

    uint32_t tail = s_log_ring.tail;
    uint32_t head = s_log_ring.head;

    if (tail == head) {
        return BP_STATUS_QUEUE_EMPTY;
    }

    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *entry = s_log_ring.entries[tail & (BP_LOG_RING_CAPACITY - 1)];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    s_log_ring.tail = tail + 1;

    return BP_STATUS_OK;
}

uint32_t bp_log_drain_to_console(void) {
    uint32_t drained = 0;
    bp_log_entry_t entry;

    while (bp_log_pop(&entry) == BP_STATUS_OK) {
        printf("[%u.%03u][C%u][%s][%s] %s\r\n",
            (unsigned)(entry.timestamp_ms / 1000),
            (unsigned)(entry.timestamp_ms % 1000),
            (unsigned)entry.core,
            bp_log_level_str((bp_log_level_t)entry.level),
            entry.tag[0] ? entry.tag : "SYS",
            entry.msg
        );
        drained++;
    }
    return drained;
}

uint32_t bp_log_count(void) {
    uint32_t head = s_log_ring.head;
    uint32_t tail = s_log_ring.tail;
    return (head - tail);
}

uint32_t bp_log_drops(void) {
    return s_log_ring.drops;
}

const char* bp_log_level_str(bp_log_level_t level) {
    switch (level) {
        case BP_LOG_DEBUG: return "DEBUG";
        case BP_LOG_INFO:  return "INFO";
        case BP_LOG_WARN:  return "WARN";
        case BP_LOG_ERROR: return "ERROR";
        default:           return "LOG";
    }
}
