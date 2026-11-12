#ifndef _TF_CARD_H_
#define _TF_CARD_H_

/* SPI pin assignment */

#include <stdint.h>
#include <stdbool.h>
#include "fatfs/ff.h" /* Obtains integer types */

typedef enum {
    TF_CARD_ADDR_BYTE = 0,    /* Standard Capacity (SDSC, byte addressed, <= 2GB) */
    TF_CARD_ADDR_BLOCK,       /* High/Extended Capacity (SDHC/SDXC, 512-byte block addressed) */
    TF_CARD_ADDR_UNSUPPORTED  /* SDUC (CSD v3) or unsupported address width */
} tf_card_addr_mode_t;

typedef struct {
    uint64_t sector_count;            /* Authoritative total 512-byte sectors (64-bit to prevent 2^32 truncation) */
    uint32_t media_generation;        /* Increments on mount/insert; 0 = invalid/removed */
    tf_card_addr_mode_t addr_mode;    /* Addressing mode */
    uint16_t sector_size;             /* Always 512 */
    uint8_t csd_version;              /* 1 = CSD v1.0, 2 = CSD v2.0, 3 = CSD v3.0, 0 = invalid */
    bool valid;                       /* True if metadata successfully parsed and within bounds */
    bool write_protected;             /* True if write protection detected in CSD */
} tf_card_metadata_t;

DSTATUS diskio_initialize(BYTE pdrv);
DSTATUS diskio_status(BYTE pdrv);
DRESULT diskio_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT diskio_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT diskio_ioctl(BYTE pdrv, BYTE cmd, void *buff);

/**
 * @brief Parse and validate raw 16-byte CSD register from card
 * @param csd 16-byte raw CSD data
 * @param out_meta Output metadata structure
 * @return true if valid and supported, false if malformed, zero, or unsupported version
 */
bool tf_card_parse_csd(const uint8_t csd[16], tf_card_metadata_t *out_meta);

/**
 * @brief Get authoritative metadata for currently inserted/initialized card
 * @param out_meta Pointer to caller buffer
 * @return true if valid card metadata available, false if removed or uninitialized
 */
bool tf_card_get_metadata(tf_card_metadata_t *out_meta);

/**
 * @brief Invalidate card metadata and reset media generation (e.g., on card removal/unmount)
 */
void tf_card_invalidate_metadata(void);

/**
 * @brief Get current media generation token
 * @return Monotonic token (0 if no valid card)
 */
uint32_t tf_card_get_media_generation(void);

bool tf_card_init_dma(void);
void tf_card_deinit_dma(void);
int32_t tf_card_get_dma_rx_channel(void);
int32_t tf_card_get_dma_tx_channel(void);

#endif // _TF_CARD_H_
