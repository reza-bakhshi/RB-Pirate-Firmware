/* Modified by RB-Pirate contributors, 2026. Fixed, aligned FlatCC block pools. */
#include "services/bp_flatcc_memory.h"
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#if defined(BP_EMBEDDED)
#include "pico/stdlib.h"
#endif

/* Stable blocks preserve FlatCC emitter page links and builder references.
 * 14 KiB payload; no fragmentation, merging or unbounded free-list traversal. */
static _Alignas(max_align_t) uint8_t small[8][256];
static _Alignas(max_align_t) uint8_t medium[4][1024];
static _Alignas(max_align_t) uint8_t large[2][4096];
static uint16_t used;
static uint32_t failures;
static bool on_control_core(void) {
#if defined(BP_EMBEDDED)
    return get_core_num() == 0;
#else
    return true;
#endif
}

static uint8_t *block(unsigned index, size_t *capacity) {
    if (index < 8) { *capacity = 256; return small[index]; }
    if (index < 12) { *capacity = 1024; return medium[index - 8]; }
    *capacity = 4096; return large[index - 12];
}
static int find(const void *ptr) {
    for (unsigned i = 0; i < 14; i++) {
        size_t capacity;
        if (block(i, &capacity) == ptr && (used & (1u << i))) return (int)i;
    }
    return -1;
}
void *bp_flatcc_alloc(size_t size) {
    if (!on_control_core()) { __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED); return NULL; }
    if (!size) return NULL;
    for (unsigned i = 0; i < 14; i++) {
        size_t capacity;
        uint8_t *ptr = block(i, &capacity);
        if (size <= capacity && !(used & (1u << i))) {
            used |= (uint16_t)(1u << i);
            return ptr;
        }
    }
    __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED);
    return NULL;
}
void bp_flatcc_free(void *ptr) {
    if (!on_control_core() || !ptr) return;
    int i = find(ptr);
    if (i >= 0) used &= (uint16_t)~(1u << i);
}
void *bp_flatcc_realloc(void *ptr, size_t size) {
    if (!on_control_core()) { __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED); return NULL; }
    if (!ptr) return bp_flatcc_alloc(size);
    int i = find(ptr);
    if (i < 0) { __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED); return NULL; }
    if (!size) { bp_flatcc_free(ptr); return NULL; }
    size_t capacity;
    block((unsigned)i, &capacity);
    if (size <= capacity) return ptr;
    void *replacement = bp_flatcc_alloc(size);
    if (!replacement) return NULL; /* Keep old allocation intact. */
    memcpy(replacement, ptr, capacity);
    bp_flatcc_free(ptr);
    return replacement;
}
void *bp_flatcc_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) { __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED); return NULL; }
    void *ptr = bp_flatcc_alloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}
size_t bp_flatcc_live_blocks(void) {
    size_t count = 0;
    for (unsigned i = 0; i < 14; i++) if (used & (1u << i)) count++;
    return count;
}
uint32_t bp_flatcc_allocation_failures(void) { return __atomic_load_n(&failures, __ATOMIC_RELAXED); }

void *bp_flatcc_aligned_alloc(size_t alignment, size_t size) {
    if (!alignment || (alignment & (alignment - 1)) || alignment > 4096) return NULL;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    size_t overhead = alignment - 1 + sizeof(void*);
    if (size > SIZE_MAX - overhead) return NULL;
    uint8_t *raw = bp_flatcc_alloc(size + overhead);
    if (!raw) return NULL;
    uintptr_t address = ((uintptr_t)raw + overhead) & ~(uintptr_t)(alignment - 1);
    void *ptr = (void*)address;
    ((void**)ptr)[-1] = raw;
    return ptr;
}
void bp_flatcc_aligned_free(void *ptr) {
    if (on_control_core() && ptr) bp_flatcc_free(((void**)ptr)[-1]);
}
