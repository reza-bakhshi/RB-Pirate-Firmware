/**
 * @file bp_resource.h
 * @brief Hardware resource lease manager for RP2350 (Bus Pirate 6).
 *
 * Provides safe, non-panicking, tracked lease acquisition and transactional
 * teardown for PIO state machines, DMA channels, IRQ handlers, communication
 * peripherals, and the 128 KB big buffer arena.
 */

#ifndef BP_RESOURCE_H
#define BP_RESOURCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel/bp_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resource classes */
typedef enum bp_res_class {
    BP_RES_CLASS_PIO_SM = 0,    /* PIO State Machines (0..11) */
    BP_RES_CLASS_DMA_CH,        /* DMA Channels (0..15) */
    BP_RES_CLASS_IRQ,           /* Hardware IRQ lines (0..63) */
    BP_RES_CLASS_PERIPH,        /* Hardware peripheral controllers (0..7) */
    BP_RES_CLASS_BIG_BUFFER,    /* 128KB Big buffer arena */
    BP_RES_CLASS_COUNT
} bp_res_class_t;

/* Standard owner IDs */
#define BP_RES_OWNER_NONE         0
#define BP_RES_OWNER_SYSTEM       1
#define BP_RES_OWNER_RGB          2
#define BP_RES_OWNER_UART_DBG     3
#define BP_RES_OWNER_STORAGE      4
#define BP_RES_OWNER_LOGIC        5
#define BP_RES_OWNER_SCOPE        6
#define BP_RES_OWNER_FALA         7
#define BP_RES_OWNER_JOB          8
#define BP_RES_OWNER_EDITOR       9
#define BP_RES_OWNER_TXTEST       10
#define BP_RES_OWNER_LCDTAP       11
#define BP_RES_OWNER_BINMODE_BASE 50
#define BP_RES_OWNER_MODE_BASE    100

/* Hardware limits */
#define BP_RES_MAX_PIOS           3
#define BP_RES_SM_PER_PIO         4
#define BP_RES_TOTAL_PIO_SM       (BP_RES_MAX_PIOS * BP_RES_SM_PER_PIO) /* 12 */
#define BP_RES_MAX_DMA_CHANNELS   16
#define BP_RES_MAX_IRQS           64
#define BP_RES_MAX_PERIPHS        8
#define BP_RES_MAX_NAME_LEN       16

/* Peripheral indices for BP_RES_CLASS_PERIPH (0..BP_RES_MAX_PERIPHS-1) */
#define BP_RES_PERIPH_UART0       0
#define BP_RES_PERIPH_UART1       1
#define BP_RES_PERIPH_SPI0        2
#define BP_RES_PERIPH_SPI1        3
#define BP_RES_PERIPH_I2C0        4
#define BP_RES_PERIPH_I2C1        5

#define BP_BIG_BUFFER_SIZE        (128 * 1024)

/* Resource lease descriptor */
typedef struct bp_res_lease {
    bp_res_class_t res_class;
    uint32_t       res_id;
    uint32_t       owner_id;
    char           owner_name[BP_RES_MAX_NAME_LEN];
    bool           acquired;
} bp_res_lease_t;

typedef void (*bp_irq_handler_t)(void);

/**
 * @brief Initialize resource registry tables.
 */
bp_status_t bp_res_init(void);

/**
 * @brief Acquire a specific hardware resource by class and index.
 *
 * @param res_class Resource class.
 * @param res_id Resource index within class.
 * @param owner_id Identifier of acquiring owner.
 * @param name Optional label/tag for diagnostics.
 * @return BP_STATUS_OK on success, BP_STATUS_NO_RESOURCE if contested,
 *         BP_STATUS_INVALID_PARAM if index out of bounds.
 */
bp_status_t bp_res_acquire(bp_res_class_t res_class, uint32_t res_id,
                           uint32_t owner_id, const char* name);

/**
 * @brief Release a previously acquired resource.
 */
bp_status_t bp_res_release(bp_res_class_t res_class, uint32_t res_id,
                           uint32_t owner_id);

/**
 * @brief Find and claim the first unused PIO state machine on a specific PIO block.
 *
 * @param pio_idx PIO instance (0, 1, or 2).
 * @param owner_id Identifier of acquiring owner.
 * @param name Diagnostic label.
 * @param out_sm Pointer to receive claimed SM index (0..3).
 * @return BP_STATUS_OK, or BP_STATUS_NO_RESOURCE if all SMs are busy.
 */
bp_status_t bp_res_claim_unused_pio_sm(uint32_t pio_idx, uint32_t owner_id,
                                       const char* name, uint32_t* out_sm);

/**
 * @brief Find and claim the first unused DMA channel.
 *
 * @param owner_id Identifier of acquiring owner.
 * @param name Diagnostic label.
 * @param out_ch Pointer to receive claimed DMA channel index.
 * @return BP_STATUS_OK, or BP_STATUS_NO_RESOURCE if all channels busy.
 */
bp_status_t bp_res_claim_unused_dma_ch(uint32_t owner_id, const char* name,
                                       uint32_t* out_ch);

/**
 * @brief Transactionally release all resources leased by an owner.
 *
 * Traverses all resource classes, unclaims hardware blocks, and resets leases.
 *
 * @param owner_id Owner ID to release.
 * @return Total number of resources released.
 */
uint32_t bp_res_release_all(uint32_t owner_id);

/**
 * @brief Check if a specific resource is free.
 */
bool bp_res_is_free(bp_res_class_t res_class, uint32_t res_id);

/**
 * @brief Query current owner ID of a resource.
 */
uint32_t bp_res_get_owner(bp_res_class_t res_class, uint32_t res_id);

/**
 * @brief Query current owner name of a resource.
 */
const char* bp_res_get_owner_name(bp_res_class_t res_class, uint32_t res_id);

/**
 * @brief Count how many resources are currently held by an owner.
 */
uint32_t bp_res_count_owned(uint32_t owner_id);

/**
 * @brief Acquire the 128 KB big buffer arena.
 *
 * Thread-safe, non-panicking, returns NULL if already allocated.
 */
bp_status_t bp_res_acquire_buffer(uint32_t owner_id, const char* name,
                                  size_t size, uint8_t** out_ptr);

/**
 * @brief Release the big buffer arena.
 */
bp_status_t bp_res_release_buffer(uint32_t owner_id, uint8_t* ptr);

/**
 * @brief Get raw pointer to the big buffer arena without claiming.
 */
uint8_t* bp_res_get_big_buffer_ptr(void);

/**
 * @brief Acquire a hardware IRQ vector and install handler safely.
 */
bp_status_t bp_res_acquire_irq(uint32_t irq_num, uint32_t owner_id,
                               const char* name, bp_irq_handler_t handler);

/**
 * @brief Release a hardware IRQ vector and disable interrupt line.
 */
bp_status_t bp_res_release_irq(uint32_t irq_num, uint32_t owner_id);

/**
 * @brief Print formatted resource allocation table to stdout.
 */
void bp_res_print_table(void);

#ifdef __cplusplus
}
#endif

#endif /* BP_RESOURCE_H */
