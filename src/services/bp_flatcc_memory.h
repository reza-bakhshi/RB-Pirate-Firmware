/* Modified by RB-Pirate contributors, 2026. Bounded FlatCC runtime storage. */
#ifndef BP_FLATCC_MEMORY_H
#define BP_FLATCC_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Core 0 owns FlatCC. No libc heap, ISR allocation or concurrent builders. */
void *bp_flatcc_alloc(size_t size);
void *bp_flatcc_calloc(size_t count, size_t size);
void *bp_flatcc_realloc(void *ptr, size_t size);
void bp_flatcc_free(void *ptr);
void *bp_flatcc_aligned_alloc(size_t alignment, size_t size);
void bp_flatcc_aligned_free(void *ptr);
size_t bp_flatcc_live_blocks(void);
uint32_t bp_flatcc_allocation_failures(void);
#ifdef __cplusplus
}
#endif
#endif
