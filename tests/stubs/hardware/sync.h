/* Stub: hardware/sync.h for host-side testing */
#ifndef _HARDWARE_SYNC_H
#define _HARDWARE_SYNC_H
#include <stdint.h>
#include <stdbool.h>

typedef int spin_lock_t;
typedef unsigned int uint;

uint32_t spin_lock_blocking(spin_lock_t *lock);
void spin_unlock(spin_lock_t *lock, uint32_t save);
void spin_lock_unsafe_blocking(spin_lock_t *lock);
void spin_unlock_unsafe(spin_lock_t *lock);
uint spin_lock_claim_unused(bool required);
spin_lock_t* spin_lock_init(uint lock_num);

#endif
