#ifndef PIRATE_MEM_POOL_H
#define PIRATE_MEM_POOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Fixed-Block Static Memory Pool (bp_pool_t)
 *
 * O(1) allocation and bounded O(capacity) checked deallocation for fixed-size objects.
 * Intrusive free-list stored directly within idle blocks; zero overhead.
 * ========================================================================== */

typedef struct bp_pool_node {
    struct bp_pool_node *next;
} bp_pool_node_t;

typedef struct {
    uint8_t *buffer;
    size_t block_size;
    size_t capacity;
    size_t used_count;
    size_t peak_count;
    bp_pool_node_t *free_list;
} bp_pool_t;

/**
 * @brief Initialize a static memory pool over caller-supplied memory.
 * @param pool Pointer to bp_pool_t structure.
 * @param buffer Contiguous memory buffer for pool storage.
 * @param buffer_size Total size of the buffer in bytes.
 * @param block_size Size of each individual block in bytes.
 * @return true if initialized successfully, false if arguments invalid.
 */
bool bp_pool_init(bp_pool_t *pool, void *buffer, size_t buffer_size, size_t block_size);

/**
 * @brief Allocate a single block from the pool in O(1) time.
 * @param pool Pointer to initialized pool.
 * @return Pointer to allocated block, or NULL if capacity reached.
 */
void *bp_pool_alloc(bp_pool_t *pool);

/**
 * @brief Return a block to the pool after bounded duplicate-free validation.
 * @param pool Pointer to initialized pool.
 * @param ptr Pointer previously returned by bp_pool_alloc().
 * @return true if freed, false if ptr is NULL or outside pool bounds.
 */
bool bp_pool_free(bp_pool_t *pool, void *ptr);

size_t bp_pool_get_used(const bp_pool_t *pool);
size_t bp_pool_get_free(const bp_pool_t *pool);
size_t bp_pool_get_peak(const bp_pool_t *pool);


/* ==========================================================================
 * Linear Bump Arena (bp_arena_t)
 *
 * Fast contiguous allocation with rewind/reset capability for bounded scopes.
 * ========================================================================== */

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
    size_t peak_offset;
} bp_arena_t;

/**
 * @brief Initialize an arena over caller-supplied memory.
 * @param arena Pointer to bp_arena_t structure.
 * @param buffer Contiguous memory buffer for arena storage.
 * @param capacity Total size of the buffer in bytes.
 * @return true if initialized successfully, false if arguments invalid.
 */
bool bp_arena_init(bp_arena_t *arena, void *buffer, size_t capacity);

/**
 * @brief Allocate bytes from the arena with alignment.
 * @param arena Pointer to initialized arena.
 * @param size Number of bytes to allocate.
 * @param align Power-of-two alignment (pass 0 for default pointer alignment).
 * @return Pointer to allocated bytes, or NULL if arena exhausted.
 */
void *bp_arena_alloc(bp_arena_t *arena, size_t size, size_t align);

/**
 * @brief Reset arena allocation offset to zero.
 * @param arena Pointer to initialized arena.
 */
void bp_arena_reset(bp_arena_t *arena);

size_t bp_arena_get_used(const bp_arena_t *arena);
size_t bp_arena_get_free(const bp_arena_t *arena);
size_t bp_arena_get_peak(const bp_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* PIRATE_MEM_POOL_H */
