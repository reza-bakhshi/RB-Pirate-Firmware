#include "pirate/mem_pool.h"
#include <string.h>

#define ALIGN_UP(val, align) (((val) + ((align) - 1)) & ~((align) - 1))

/* ==========================================================================
 * Fixed-Block Static Memory Pool Implementation
 * ========================================================================== */

bool bp_pool_init(bp_pool_t *pool, void *buffer, size_t buffer_size, size_t block_size) {
    if (!pool || !buffer || buffer_size == 0 || block_size == 0) {
        return false;
    }

    size_t align = sizeof(void *);
    size_t aligned_block = ALIGN_UP(block_size, align);
    if (aligned_block < sizeof(bp_pool_node_t)) {
        aligned_block = sizeof(bp_pool_node_t);
    }

    size_t count = buffer_size / aligned_block;
    if (count == 0) {
        return false;
    }

    pool->buffer = (uint8_t *)buffer;
    pool->block_size = aligned_block;
    pool->capacity = count;
    pool->used_count = 0;
    pool->peak_count = 0;
    pool->free_list = NULL;

    /* Build intrusive free-list linking all blocks */
    for (size_t i = 0; i < count; i++) {
        bp_pool_node_t *node = (bp_pool_node_t *)(pool->buffer + i * aligned_block);
        node->next = pool->free_list;
        pool->free_list = node;
    }

    return true;
}

void *bp_pool_alloc(bp_pool_t *pool) {
    if (!pool || !pool->free_list) {
        return NULL;
    }

    bp_pool_node_t *node = pool->free_list;
    pool->free_list = node->next;
    pool->used_count++;
    if (pool->used_count > pool->peak_count) {
        pool->peak_count = pool->used_count;
    }

    return (void *)node;
}

bool bp_pool_free(bp_pool_t *pool, void *ptr) {
    if (!pool || !ptr) {
        return false;
    }

    uint8_t *p = (uint8_t *)ptr;
    if (p < pool->buffer || p >= pool->buffer + pool->capacity * pool->block_size) {
        return false; /* Pointer outside pool buffer */
    }

    size_t offset = (size_t)(p - pool->buffer);
    if ((offset % pool->block_size) != 0) {
        return false; /* Misaligned pointer */
    }

    bp_pool_node_t *node = (bp_pool_node_t *)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    if (pool->used_count > 0) {
        pool->used_count--;
    }

    return true;
}

size_t bp_pool_get_used(const bp_pool_t *pool) {
    return pool ? pool->used_count : 0;
}

size_t bp_pool_get_free(const bp_pool_t *pool) {
    return pool ? (pool->capacity - pool->used_count) : 0;
}

size_t bp_pool_get_peak(const bp_pool_t *pool) {
    return pool ? pool->peak_count : 0;
}


/* ==========================================================================
 * Linear Bump Arena Implementation
 * ========================================================================== */

bool bp_arena_init(bp_arena_t *arena, void *buffer, size_t capacity) {
    if (!arena || !buffer || capacity == 0) {
        return false;
    }

    arena->buffer = (uint8_t *)buffer;
    arena->capacity = capacity;
    arena->offset = 0;
    arena->peak_offset = 0;
    return true;
}

void *bp_arena_alloc(bp_arena_t *arena, size_t size, size_t align) {
    if (!arena || size == 0) {
        return NULL;
    }

    if (align == 0) {
        align = sizeof(void *);
    }

    size_t current_addr = (size_t)(arena->buffer + arena->offset);
    size_t aligned_addr = ALIGN_UP(current_addr, align);
    size_t padding = aligned_addr - current_addr;

    if (arena->offset + padding + size > arena->capacity) {
        return NULL; /* Out of space */
    }

    arena->offset += padding;
    void *ptr = (void *)(arena->buffer + arena->offset);
    arena->offset += size;

    if (arena->offset > arena->peak_offset) {
        arena->peak_offset = arena->offset;
    }

    return ptr;
}

void bp_arena_reset(bp_arena_t *arena) {
    if (arena) {
        arena->offset = 0;
    }
}

size_t bp_arena_get_used(const bp_arena_t *arena) {
    return arena ? arena->offset : 0;
}

size_t bp_arena_get_free(const bp_arena_t *arena) {
    return arena ? (arena->capacity - arena->offset) : 0;
}

size_t bp_arena_get_peak(const bp_arena_t *arena) {
    return arena ? arena->peak_offset : 0;
}
