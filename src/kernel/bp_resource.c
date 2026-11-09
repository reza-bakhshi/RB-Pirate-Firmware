/**
 * @file bp_resource.c
 * @brief Implementation of hardware resource lease manager for RP2350.
 */

#include "kernel/bp_resource.h"
#include <string.h>
#include <stdio.h>

#if __has_include("pico/stdlib.h") && !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#define HAVE_PICO_SDK 1
#endif

/* Static resource tables */
static bp_res_lease_t s_pio_sm[BP_RES_TOTAL_PIO_SM];
static bp_res_lease_t s_dma_ch[BP_RES_MAX_DMA_CHANNELS];
static bp_res_lease_t s_irq[BP_RES_MAX_IRQS];
static bp_res_lease_t s_periph[BP_RES_MAX_PERIPHS];
static bp_res_lease_t s_big_buffer_lease;

/* Statically allocated 128KB Big Buffer */
static uint8_t s_big_buffer[BP_BIG_BUFFER_SIZE] __attribute__((aligned(4)));

static bool s_initialized = false;

/* Interrupt / critical section guard */
static inline uint32_t bp_res_enter_critical(void) {
#if defined(HAVE_PICO_SDK)
    return save_and_disable_interrupts();
#else
    return 0;
#endif
}

static inline void bp_res_exit_critical(uint32_t status) {
#if defined(HAVE_PICO_SDK)
    restore_interrupts(status);
#else
    (void)status;
#endif
}

#if defined(HAVE_PICO_SDK)
static inline PIO bp_res_get_pio_hw(uint32_t pio_idx) {
    if (pio_idx == 0) return pio0;
    if (pio_idx == 1) return pio1;
#if BP_RES_MAX_PIOS > 2
    if (pio_idx == 2) return pio2;
#endif
    return NULL;
}
#endif

bp_status_t bp_res_init(void) {
    uint32_t ints = bp_res_enter_critical();

    memset(s_pio_sm, 0, sizeof(s_pio_sm));
    for (uint32_t i = 0; i < BP_RES_TOTAL_PIO_SM; i++) {
        s_pio_sm[i].res_class = BP_RES_CLASS_PIO_SM;
        s_pio_sm[i].res_id = i;
        s_pio_sm[i].owner_id = BP_RES_OWNER_NONE;
        s_pio_sm[i].acquired = false;
    }

    memset(s_dma_ch, 0, sizeof(s_dma_ch));
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        s_dma_ch[i].res_class = BP_RES_CLASS_DMA_CH;
        s_dma_ch[i].res_id = i;
        s_dma_ch[i].owner_id = BP_RES_OWNER_NONE;
        s_dma_ch[i].acquired = false;
    }

    memset(s_irq, 0, sizeof(s_irq));
    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        s_irq[i].res_class = BP_RES_CLASS_IRQ;
        s_irq[i].res_id = i;
        s_irq[i].owner_id = BP_RES_OWNER_NONE;
        s_irq[i].acquired = false;
    }

    memset(s_periph, 0, sizeof(s_periph));
    for (uint32_t i = 0; i < BP_RES_MAX_PERIPHS; i++) {
        s_periph[i].res_class = BP_RES_CLASS_PERIPH;
        s_periph[i].res_id = i;
        s_periph[i].owner_id = BP_RES_OWNER_NONE;
        s_periph[i].acquired = false;
    }

    memset(&s_big_buffer_lease, 0, sizeof(s_big_buffer_lease));
    s_big_buffer_lease.res_class = BP_RES_CLASS_BIG_BUFFER;
    s_big_buffer_lease.res_id = 0;
    s_big_buffer_lease.owner_id = BP_RES_OWNER_NONE;
    s_big_buffer_lease.acquired = false;

    s_initialized = true;

    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

static bp_res_lease_t* bp_res_lookup(bp_res_class_t res_class, uint32_t res_id) {
    if (!s_initialized) bp_res_init();

    switch (res_class) {
        case BP_RES_CLASS_PIO_SM:
            if (res_id >= BP_RES_TOTAL_PIO_SM) return NULL;
            return &s_pio_sm[res_id];

        case BP_RES_CLASS_DMA_CH:
            if (res_id >= BP_RES_MAX_DMA_CHANNELS) return NULL;
            return &s_dma_ch[res_id];

        case BP_RES_CLASS_IRQ:
            if (res_id >= BP_RES_MAX_IRQS) return NULL;
            return &s_irq[res_id];

        case BP_RES_CLASS_PERIPH:
            if (res_id >= BP_RES_MAX_PERIPHS) return NULL;
            return &s_periph[res_id];

        case BP_RES_CLASS_BIG_BUFFER:
            if (res_id != 0) return NULL;
            return &s_big_buffer_lease;

        default:
            return NULL;
    }
}

bp_status_t bp_res_acquire(bp_res_class_t res_class, uint32_t res_id,
                           uint32_t owner_id, const char* name) {
    if (owner_id == BP_RES_OWNER_NONE) return BP_STATUS_INVALID_PARAM;

    uint32_t ints = bp_res_enter_critical();
    bp_res_lease_t* lease = bp_res_lookup(res_class, res_id);
    if (!lease) {
        bp_res_exit_critical(ints);
        return BP_STATUS_INVALID_PARAM;
    }

    if (lease->acquired) {
        bp_res_exit_critical(ints);
        return BP_STATUS_NO_RESOURCE;
    }

#if defined(HAVE_PICO_SDK)
    /* Verify with hardware claim state */
    if (res_class == BP_RES_CLASS_PIO_SM) {
        uint32_t pio_idx = res_id / BP_RES_SM_PER_PIO;
        uint32_t sm_idx  = res_id % BP_RES_SM_PER_PIO;
        PIO pio = bp_res_get_pio_hw(pio_idx);
        if (!pio || pio_sm_is_claimed(pio, sm_idx)) {
            bp_res_exit_critical(ints);
            return BP_STATUS_NO_RESOURCE;
        }
        pio_sm_claim(pio, sm_idx);
    } else if (res_class == BP_RES_CLASS_DMA_CH) {
        if (dma_channel_is_claimed(res_id)) {
            bp_res_exit_critical(ints);
            return BP_STATUS_NO_RESOURCE;
        }
        dma_channel_claim(res_id);
    }
#endif

    lease->acquired = true;
    lease->owner_id = owner_id;
    if (name) {
        strncpy(lease->owner_name, name, sizeof(lease->owner_name) - 1);
        lease->owner_name[sizeof(lease->owner_name) - 1] = '\0';
    } else {
        lease->owner_name[0] = '\0';
    }

    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

bp_status_t bp_res_release(bp_res_class_t res_class, uint32_t res_id,
                           uint32_t owner_id) {
    uint32_t ints = bp_res_enter_critical();
    bp_res_lease_t* lease = bp_res_lookup(res_class, res_id);
    if (!lease) {
        bp_res_exit_critical(ints);
        return BP_STATUS_INVALID_PARAM;
    }

    if (!lease->acquired) {
        bp_res_exit_critical(ints);
        return BP_STATUS_OK;
    }

    /* If owner_id specified, must match */
    if (owner_id != BP_RES_OWNER_NONE && lease->owner_id != owner_id) {
        bp_res_exit_critical(ints);
        return BP_STATUS_INVALID_PARAM;
    }

#if defined(HAVE_PICO_SDK)
    if (res_class == BP_RES_CLASS_PIO_SM) {
        uint32_t pio_idx = res_id / BP_RES_SM_PER_PIO;
        uint32_t sm_idx  = res_id % BP_RES_SM_PER_PIO;
        PIO pio = bp_res_get_pio_hw(pio_idx);
        if (pio && pio_sm_is_claimed(pio, sm_idx)) {
            pio_sm_set_enabled(pio, sm_idx, false);
            pio_sm_unclaim(pio, sm_idx);
        }
    } else if (res_class == BP_RES_CLASS_DMA_CH) {
        if (dma_channel_is_claimed(res_id)) {
            dma_channel_abort(res_id);
            dma_channel_unclaim(res_id);
        }
    } else if (res_class == BP_RES_CLASS_IRQ) {
        irq_set_enabled(res_id, false);
    }
#endif

    lease->acquired = false;
    lease->owner_id = BP_RES_OWNER_NONE;
    lease->owner_name[0] = '\0';

    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

bp_status_t bp_res_claim_unused_pio_sm(uint32_t pio_idx, uint32_t owner_id,
                                       const char* name, uint32_t* out_sm) {
    if (!out_sm || pio_idx >= BP_RES_MAX_PIOS || owner_id == BP_RES_OWNER_NONE) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t ints = bp_res_enter_critical();
    if (!s_initialized) bp_res_init();

#if defined(HAVE_PICO_SDK)
    PIO pio = bp_res_get_pio_hw(pio_idx);
    if (!pio) {
        bp_res_exit_critical(ints);
        return BP_STATUS_INVALID_PARAM;
    }
#endif

    for (uint32_t sm = 0; sm < BP_RES_SM_PER_PIO; sm++) {
        uint32_t global_idx = (pio_idx * BP_RES_SM_PER_PIO) + sm;
        bp_res_lease_t* lease = &s_pio_sm[global_idx];

        if (!lease->acquired) {
#if defined(HAVE_PICO_SDK)
            if (pio_sm_is_claimed(pio, sm)) {
                continue;
            }
            pio_sm_claim(pio, sm);
#endif
            lease->acquired = true;
            lease->owner_id = owner_id;
            if (name) {
                strncpy(lease->owner_name, name, sizeof(lease->owner_name) - 1);
                lease->owner_name[sizeof(lease->owner_name) - 1] = '\0';
            } else {
                lease->owner_name[0] = '\0';
            }

            *out_sm = sm;
            bp_res_exit_critical(ints);
            return BP_STATUS_OK;
        }
    }

    bp_res_exit_critical(ints);
    return BP_STATUS_NO_RESOURCE;
}

bp_status_t bp_res_claim_unused_dma_ch(uint32_t owner_id, const char* name,
                                       uint32_t* out_ch) {
    if (!out_ch || owner_id == BP_RES_OWNER_NONE) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t ints = bp_res_enter_critical();
    if (!s_initialized) bp_res_init();

    for (uint32_t ch = 0; ch < BP_RES_MAX_DMA_CHANNELS; ch++) {
        bp_res_lease_t* lease = &s_dma_ch[ch];

        if (!lease->acquired) {
#if defined(HAVE_PICO_SDK)
            if (dma_channel_is_claimed(ch)) {
                continue;
            }
            dma_channel_claim(ch);
#endif
            lease->acquired = true;
            lease->owner_id = owner_id;
            if (name) {
                strncpy(lease->owner_name, name, sizeof(lease->owner_name) - 1);
                lease->owner_name[sizeof(lease->owner_name) - 1] = '\0';
            } else {
                lease->owner_name[0] = '\0';
            }

            *out_ch = ch;
            bp_res_exit_critical(ints);
            return BP_STATUS_OK;
        }
    }

    bp_res_exit_critical(ints);
    return BP_STATUS_NO_RESOURCE;
}

uint32_t bp_res_release_all(uint32_t owner_id) {
    if (owner_id == BP_RES_OWNER_NONE) return 0;

    uint32_t released_count = 0;

    for (uint32_t i = 0; i < BP_RES_TOTAL_PIO_SM; i++) {
        if (s_pio_sm[i].acquired && s_pio_sm[i].owner_id == owner_id) {
            bp_res_release(BP_RES_CLASS_PIO_SM, i, owner_id);
            released_count++;
        }
    }

    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        if (s_dma_ch[i].acquired && s_dma_ch[i].owner_id == owner_id) {
            bp_res_release(BP_RES_CLASS_DMA_CH, i, owner_id);
            released_count++;
        }
    }

    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        if (s_irq[i].acquired && s_irq[i].owner_id == owner_id) {
            bp_res_release(BP_RES_CLASS_IRQ, i, owner_id);
            released_count++;
        }
    }

    for (uint32_t i = 0; i < BP_RES_MAX_PERIPHS; i++) {
        if (s_periph[i].acquired && s_periph[i].owner_id == owner_id) {
            bp_res_release(BP_RES_CLASS_PERIPH, i, owner_id);
            released_count++;
        }
    }

    if (s_big_buffer_lease.acquired && s_big_buffer_lease.owner_id == owner_id) {
        bp_res_release(BP_RES_CLASS_BIG_BUFFER, 0, owner_id);
        released_count++;
    }

    return released_count;
}

bool bp_res_is_free(bp_res_class_t res_class, uint32_t res_id) {
    bp_res_lease_t* lease = bp_res_lookup(res_class, res_id);
    if (!lease) return false;
    return !lease->acquired;
}

uint32_t bp_res_get_owner(bp_res_class_t res_class, uint32_t res_id) {
    bp_res_lease_t* lease = bp_res_lookup(res_class, res_id);
    if (!lease || !lease->acquired) return BP_RES_OWNER_NONE;
    return lease->owner_id;
}

const char* bp_res_get_owner_name(bp_res_class_t res_class, uint32_t res_id) {
    bp_res_lease_t* lease = bp_res_lookup(res_class, res_id);
    if (!lease || !lease->acquired) return "";
    return lease->owner_name;
}

uint32_t bp_res_count_owned(uint32_t owner_id) {
    if (owner_id == BP_RES_OWNER_NONE) return 0;
    if (!s_initialized) bp_res_init();

    uint32_t count = 0;
    for (uint32_t i = 0; i < BP_RES_TOTAL_PIO_SM; i++) {
        if (s_pio_sm[i].acquired && s_pio_sm[i].owner_id == owner_id) count++;
    }
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        if (s_dma_ch[i].acquired && s_dma_ch[i].owner_id == owner_id) count++;
    }
    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        if (s_irq[i].acquired && s_irq[i].owner_id == owner_id) count++;
    }
    for (uint32_t i = 0; i < BP_RES_MAX_PERIPHS; i++) {
        if (s_periph[i].acquired && s_periph[i].owner_id == owner_id) count++;
    }
    if (s_big_buffer_lease.acquired && s_big_buffer_lease.owner_id == owner_id) {
        count++;
    }
    return count;
}

bp_status_t bp_res_acquire_buffer(uint32_t owner_id, const char* name,
                                  size_t size, uint8_t** out_ptr) {
    if (!out_ptr || size > BP_BIG_BUFFER_SIZE || owner_id == BP_RES_OWNER_NONE) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t ints = bp_res_enter_critical();
    if (!s_initialized) bp_res_init();

    if (s_big_buffer_lease.acquired) {
        bp_res_exit_critical(ints);
        return BP_STATUS_NO_RESOURCE;
    }

    s_big_buffer_lease.acquired = true;
    s_big_buffer_lease.owner_id = owner_id;
    if (name) {
        strncpy(s_big_buffer_lease.owner_name, name, sizeof(s_big_buffer_lease.owner_name) - 1);
        s_big_buffer_lease.owner_name[sizeof(s_big_buffer_lease.owner_name) - 1] = '\0';
    } else {
        s_big_buffer_lease.owner_name[0] = '\0';
    }

    *out_ptr = s_big_buffer;
    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

bp_status_t bp_res_release_buffer(uint32_t owner_id, uint8_t* ptr) {
    if (ptr != s_big_buffer) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t ints = bp_res_enter_critical();
    if (!s_big_buffer_lease.acquired) {
        bp_res_exit_critical(ints);
        return BP_STATUS_OK;
    }

    if (owner_id != BP_RES_OWNER_NONE && s_big_buffer_lease.owner_id != owner_id) {
        bp_res_exit_critical(ints);
        return BP_STATUS_INVALID_PARAM;
    }

    s_big_buffer_lease.acquired = false;
    s_big_buffer_lease.owner_id = BP_RES_OWNER_NONE;
    s_big_buffer_lease.owner_name[0] = '\0';

    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

uint8_t* bp_res_get_big_buffer_ptr(void) {
    return s_big_buffer;
}

bp_status_t bp_res_acquire_irq(uint32_t irq_num, uint32_t owner_id,
                               const char* name, bp_irq_handler_t handler) {
    if (irq_num >= BP_RES_MAX_IRQS || owner_id == BP_RES_OWNER_NONE) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t ints = bp_res_enter_critical();
    if (!s_initialized) bp_res_init();

    if (s_irq[irq_num].acquired) {
        bp_res_exit_critical(ints);
        return BP_STATUS_NO_RESOURCE;
    }

#if defined(HAVE_PICO_SDK)
    irq_set_enabled(irq_num, false);
    if (handler) {
        irq_set_exclusive_handler(irq_num, (irq_handler_t)handler);
        irq_set_enabled(irq_num, true);
    }
#else
    (void)handler;
#endif

    s_irq[irq_num].acquired = true;
    s_irq[irq_num].owner_id = owner_id;
    if (name) {
        strncpy(s_irq[irq_num].owner_name, name, sizeof(s_irq[irq_num].owner_name) - 1);
        s_irq[irq_num].owner_name[sizeof(s_irq[irq_num].owner_name) - 1] = '\0';
    } else {
        s_irq[irq_num].owner_name[0] = '\0';
    }

    bp_res_exit_critical(ints);
    return BP_STATUS_OK;
}

bp_status_t bp_res_release_irq(uint32_t irq_num, uint32_t owner_id) {
    return bp_res_release(BP_RES_CLASS_IRQ, irq_num, owner_id);
}

void bp_res_print_table(void) {
    if (!s_initialized) bp_res_init();

    printf("================ Hardware Resource Leases ================\r\n");

    /* PIO state machines */
    printf("PIO State Machines (%d total):\r\n", BP_RES_TOTAL_PIO_SM);
    for (uint32_t pio = 0; pio < BP_RES_MAX_PIOS; pio++) {
        for (uint32_t sm = 0; sm < BP_RES_SM_PER_PIO; sm++) {
            uint32_t idx = (pio * BP_RES_SM_PER_PIO) + sm;
            if (s_pio_sm[idx].acquired) {
                printf("  PIO%lu SM%lu: [ACQUIRED] owner=%lu (%s)\r\n",
                       (unsigned long)pio, (unsigned long)sm,
                       (unsigned long)s_pio_sm[idx].owner_id,
                       s_pio_sm[idx].owner_name[0] ? s_pio_sm[idx].owner_name : "unnamed");
            } else {
                printf("  PIO%lu SM%lu: [FREE]\r\n", (unsigned long)pio, (unsigned long)sm);
            }
        }
    }

    /* DMA channels */
    printf("\r\nDMA Channels (%d total):\r\n", BP_RES_MAX_DMA_CHANNELS);
    uint32_t dma_busy = 0;
    for (uint32_t ch = 0; ch < BP_RES_MAX_DMA_CHANNELS; ch++) {
        if (s_dma_ch[ch].acquired) {
            dma_busy++;
            printf("  DMA %2lu: [ACQUIRED] owner=%lu (%s)\r\n",
                   (unsigned long)ch, (unsigned long)s_dma_ch[ch].owner_id,
                   s_dma_ch[ch].owner_name[0] ? s_dma_ch[ch].owner_name : "unnamed");
        }
    }
    if (dma_busy == 0) {
        printf("  All DMA channels FREE (0/%d acquired)\r\n", BP_RES_MAX_DMA_CHANNELS);
    }

    /* Hardware IRQs */
    printf("\r\nHardware IRQ Leases:\r\n");
    uint32_t irq_busy = 0;
    for (uint32_t i = 0; i < BP_RES_MAX_IRQS; i++) {
        if (s_irq[i].acquired) {
            irq_busy++;
            printf("  IRQ %2lu: [ACQUIRED] owner=%lu (%s)\r\n",
                   (unsigned long)i, (unsigned long)s_irq[i].owner_id,
                   s_irq[i].owner_name[0] ? s_irq[i].owner_name : "unnamed");
        }
    }
    if (irq_busy == 0) {
        printf("  No exclusive IRQ leases active\r\n");
    }

    /* Big Buffer */
    printf("\r\nBig Buffer (128 KB arena):\r\n");
    if (s_big_buffer_lease.acquired) {
        printf("  Status: [ACQUIRED] owner=%lu (%s)\r\n",
               (unsigned long)s_big_buffer_lease.owner_id,
               s_big_buffer_lease.owner_name[0] ? s_big_buffer_lease.owner_name : "unnamed");
    } else {
        printf("  Status: [FREE] (131072 bytes available)\r\n");
    }

    printf("==========================================================\r\n");
}
