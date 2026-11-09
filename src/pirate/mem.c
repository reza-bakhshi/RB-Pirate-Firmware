/**
 * @file		mem.c
 * @author		Andrew Loebs
 * @brief		Implementation file of the memory management module
 *
 */
// Modified by Ian Lesnet 18 Dec 2023 for Bus Pirate 5
#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "pirate/mem.h"
#include <stdlib.h>
#include "system_config.h"
#include "kernel/bp_resource.h"

// public function definitions
uint8_t* mem_alloc(size_t size, uint32_t owner) {
    uint8_t* ptr = NULL;
    uint32_t owner_id = (owner == BP_BIG_BUFFER_NONE) ? BP_RES_OWNER_SYSTEM : owner;
    bp_status_t status = bp_res_acquire_buffer(owner_id, "mem_alloc", size, &ptr);
    if (status != BP_STATUS_OK) {
        return NULL;
    }
    system_config.big_buffer_owner = owner;
    return ptr;
}

void mem_free(uint8_t* ptr) {
    bp_status_t status = bp_res_release_buffer(0, ptr);
    if (status == BP_STATUS_OK) {
        system_config.big_buffer_owner = BP_BIG_BUFFER_NONE;
    }
}
