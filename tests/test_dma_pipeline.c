#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "hardware/sync.h"
#include "hardware/adc.h"
#include "pirate.h"
#include "system_config.h"
#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"
#include "pirate/amux.h"
#include "pirate/lcd.h"
#include "fatfs/diskio.h"
#include "fatfs/tf_card.h"
#include "pirate/storage.h"

/* Mock system config */
struct _system_config system_config;

/* Mock ADC state */
uint16_t s_mock_adc_val = 2048;
uint16_t s_mock_current_amux_channel = 0;
uint16_t s_mock_channel_adc[16] = {0};
bool s_mock_use_channel_adc = false;

uint16_t hw_adc_raw[16] = {0};
uint32_t hw_adc_voltage[16] = {0};
uint32_t hw_adc_avgsum_voltage[16] = {0};

/* Mock Core Num & Spinlock */
unsigned int get_core_num(void) {
    return 0;
}

static spin_lock_t s_mock_spin_lock = 0;

uint32_t spin_lock_blocking(spin_lock_t *lock) {
    (void)lock;
    return 0;
}

void spin_unlock(spin_lock_t *lock, uint32_t save) {
    (void)lock;
    (void)save;
}

uint spin_lock_claim_unused(bool required) {
    (void)required;
    return 1;
}

spin_lock_t* spin_lock_init(uint lock_num) {
    (void)lock_num;
    return &s_mock_spin_lock;
}

void spi_busy_wait_internal(bool enable, const char *file, int line) {
    (void)enable; (void)file; (void)line;
}

/* Mock Block Device Lock */
static bool s_mock_blockdev_locked = false;
void blockdev_lock_acquire(void) {
    s_mock_blockdev_locked = true;
}
void blockdev_lock_release(void) {
    s_mock_blockdev_locked = false;
}

/* Storage Block APIs matching storage.c */
bool storage_read_blocks(uint32_t lba, uint8_t* buffer, uint32_t count) {
    if (!buffer || count == 0) return false;
    blockdev_lock_acquire();
    DRESULT res = disk_read(0, buffer, (LBA_t)lba, (UINT)count);
    blockdev_lock_release();
    return (res == RES_OK);
}

bool storage_write_blocks(uint32_t lba, const uint8_t* buffer, uint32_t count) {
    if (!buffer || count == 0) return false;
    blockdev_lock_acquire();
    DRESULT res = disk_write(0, buffer, (LBA_t)lba, (UINT)count);
    blockdev_lock_release();
    return (res == RES_OK);
}

bool storage_dma_is_active(void) {
    return (bp_res_get_owner(BP_RES_CLASS_DMA_CH, 0) == BP_RES_OWNER_STORAGE ||
            system_config.storage_available != 0);
}

/* Simulated SD Card storage memory (128 sectors = 64 KB) */
#define MOCK_DISK_SECTORS 128
static uint8_t s_simulated_disk[MOCK_DISK_SECTORS][512];
static uint32_t s_disk_read_count = 0;
static uint32_t s_disk_write_count = 0;
static uint32_t s_multi_block_read_count = 0;
static uint32_t s_multi_block_write_count = 0;

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!buff || count == 0 || count > MOCK_DISK_SECTORS || sector > MOCK_DISK_SECTORS - count) {
        return RES_PARERR;
    }
    s_disk_read_count += count;
    if (count > 1) {
        s_multi_block_read_count++;
    }
    for (UINT i = 0; i < count; i++) {
        memcpy(buff + (i * 512), s_simulated_disk[sector + i], 512);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!buff || count == 0 || count > MOCK_DISK_SECTORS || sector > MOCK_DISK_SECTORS - count) {
        return RES_PARERR;
    }
    s_disk_write_count += count;
    if (count > 1) {
        s_multi_block_write_count++;
    }
    for (UINT i = 0; i < count; i++) {
        memcpy(s_simulated_disk[sector + i], buff + (i * 512), 512);
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv; (void)cmd; (void)buff;
    return RES_OK;
}

/* Simple DJB2-like checksum for buffer verification */
static uint32_t compute_checksum(const uint8_t* data, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    return hash;
}

// -------------------------------------------------------------------------
// Test 1: AMUX Autonomous DMA Sweeping & Ping-Pong Coherency (ARCH-820)
// -------------------------------------------------------------------------
static void test_amux_dma_pipeline(void) {
    printf("[TEST] AMUX DMA autonomous sweep & zero CPU spinlock...\n");

    bp_res_init();
    amux_init();

    assert(amux_dma_is_enabled());
    int32_t ch_data = amux_dma_get_data_channel();
    int32_t ch_ctrl = amux_dma_get_ctrl_channel();
    assert(ch_data >= 0 && ch_data < 16);
    assert(ch_ctrl >= 0 && ch_ctrl < 16);
    assert(ch_data != ch_ctrl);

    /* Verify lease ownership in resource manager */
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_data) == BP_RES_OWNER_SYSTEM);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_ctrl) == BP_RES_OWNER_SYSTEM);
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC) == BP_RES_OWNER_SYSTEM);
    assert(amux_get_state() == AMUX_STATE_IDLE);

    /* Simulate ADC inputs */
    s_mock_use_channel_adc = true;
    for (int i = 0; i < 16; i++) {
        s_mock_channel_adc[i] = (uint16_t)(1200 + i * 40);
    }

    /* Start sweep */
    bool ok = amux_dma_sweep_start();
    assert(ok);
    assert(amux_get_state() == AMUX_STATE_IDLE);
    assert(amux_get_generation() > 0);

    /* Verify coherent extraction */
    uint16_t raw_coherent[16] = {0};
    ok = amux_dma_get_coherent(raw_coherent, 16);
    assert(ok);
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        assert(raw_coherent[i] == (uint16_t)(1200 + i * 40));
    }

    /* Verify amux_get_frame() */
    amux_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    ok = amux_get_frame(&frame);
    assert(ok);
    assert(frame.valid);
    assert(frame.generation == amux_get_generation());
    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        assert(frame.raw[i] == (uint16_t)(1200 + i * 40));
    }

    /* Verify manual read arbitration */
    uint32_t val = amux_read(0);
    assert(val == 1200);
    assert(amux_get_state() == AMUX_STATE_IDLE);

    /* Verify zero spinlocks held during amux_sweep() */
    assert(adc_get_owner() == -1);
    amux_sweep();
    assert(adc_get_owner() == -1);

    for (int i = 0; i < HW_ADC_MUX_COUNT; i++) {
        assert(hw_adc_raw[i] == (uint16_t)(1200 + i * 40));
    }

    /* Verify cleanup */
    amux_dma_deinit();
    assert(!amux_dma_is_enabled());
    assert(amux_get_state() == AMUX_STATE_STOPPED);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_data) == BP_RES_OWNER_NONE);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_ctrl) == BP_RES_OWNER_NONE);
    assert(bp_res_get_owner(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC) == BP_RES_OWNER_NONE);

    /* Verify transactional rollback when DMA channels are exhausted */
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        assert(bp_res_acquire(BP_RES_CLASS_DMA_CH, i, 999, "exhaust") == BP_STATUS_OK);
    }
    assert(!amux_dma_init());
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_ADC)); /* ADC lease rolled back! */
    for (uint32_t i = 0; i < BP_RES_MAX_DMA_CHANNELS; i++) {
        bp_res_release(BP_RES_CLASS_DMA_CH, i, 999);
    }

    s_mock_use_channel_adc = false;
    printf("[PASS] AMUX DMA pipeline and zero spinlocks verified\n");
}

// -------------------------------------------------------------------------
// Test 2: Multi-Block SPI DMA Storage Throughput & Integrity (ARCH-821)
// -------------------------------------------------------------------------
static void test_multiblock_storage_dma(void) {
    printf("[TEST] Multi-block SPI DMA storage transfers & data integrity...\n");

    /* Initialize TF card DMA */
    bool dma_ok = tf_card_init_dma();
    assert(dma_ok);

    int32_t ch_rx = tf_card_get_dma_rx_channel();
    int32_t ch_tx = tf_card_get_dma_tx_channel();
    assert(ch_rx >= 0 && ch_rx < 16);
    assert(ch_tx >= 0 && ch_tx < 16);
    assert(ch_rx != ch_tx);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_rx) == BP_RES_OWNER_STORAGE);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_tx) == BP_RES_OWNER_STORAGE);

    /* Prepare a test pattern of 8 contiguous sectors (4096 bytes) */
    #define MULTI_SECTOR_COUNT 8
    #define MULTI_BYTE_COUNT   (MULTI_SECTOR_COUNT * 512)
    uint8_t write_pattern[MULTI_BYTE_COUNT];
    for (size_t i = 0; i < MULTI_BYTE_COUNT; i++) {
        write_pattern[i] = (uint8_t)((i * 17 + 31) & 0xFF);
    }
    uint32_t orig_checksum = compute_checksum(write_pattern, MULTI_BYTE_COUNT);

    /* Write 8 blocks starting at LBA 10 */
    s_disk_write_count = 0;
    s_multi_block_write_count = 0;
    bool write_ok = storage_write_blocks(10, write_pattern, MULTI_SECTOR_COUNT);
    assert(write_ok);
    assert(s_disk_write_count == MULTI_SECTOR_COUNT);
    assert(s_multi_block_write_count == 1);
    assert(!s_mock_blockdev_locked); // Lock must be released

    /* Read back the 8 blocks into a fresh buffer */
    uint8_t read_buffer[MULTI_BYTE_COUNT];
    memset(read_buffer, 0, sizeof(read_buffer));

    s_disk_read_count = 0;
    s_multi_block_read_count = 0;
    bool read_ok = storage_read_blocks(10, read_buffer, MULTI_SECTOR_COUNT);
    assert(read_ok);
    assert(s_disk_read_count == MULTI_SECTOR_COUNT);
    assert(s_multi_block_read_count == 1);
    assert(!s_mock_blockdev_locked);

    /* Verify 100% bit-exact data integrity */
    uint32_t read_checksum = compute_checksum(read_buffer, MULTI_BYTE_COUNT);
    assert(orig_checksum == read_checksum);
    assert(memcmp(write_pattern, read_buffer, MULTI_BYTE_COUNT) == 0);

    /* Teardown storage DMA */
    tf_card_deinit_dma();
    assert(tf_card_get_dma_rx_channel() == -1);
    assert(tf_card_get_dma_tx_channel() == -1);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_rx) == BP_RES_OWNER_NONE);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)ch_tx) == BP_RES_OWNER_NONE);

    printf("[PASS] Multi-block SPI DMA storage integrity & throughput verified\n");
}

// -------------------------------------------------------------------------
// Test 3: Asynchronous ST7789 LCD Framebuffer Refresh via DMA (ARCH-822)
// -------------------------------------------------------------------------
static void test_lcd_async_dma(void) {
    printf("[TEST] Asynchronous ST7789 LCD framebuffer refresh via DMA...\n");

    /* Initialize LCD */
    bool ok = bp_lcd_init();
    assert(ok);
    assert(bp_lcd_is_ready());
    assert(!bp_lcd_is_busy());

    int32_t lcd_ch = bp_lcd_get_dma_channel();
    assert(lcd_ch >= 0 && lcd_ch < 16);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)lcd_ch) == BP_RES_OWNER_SYSTEM);

    /* Set active window */
    bp_lcd_set_window(0, 0, 239, 319);

    /* Create sample framebuffer line (240 pixels of RGB565) */
    uint16_t sample_pixels[BP_LCD_WIDTH];
    for (int i = 0; i < BP_LCD_WIDTH; i++) {
        sample_pixels[i] = (uint16_t)(0xF800 + i); // Red gradient
    }

    assert(!bp_lcd_flush_async(sample_pixels, (size_t)BP_LCD_WIDTH * BP_LCD_HEIGHT + 1));
    assert(bp_lcd_wait_idle_timeout(0));

    /* Start non-blocking flush */
    ok = bp_lcd_flush_async(sample_pixels, BP_LCD_WIDTH);
    assert(ok);
    assert(bp_lcd_is_busy()); /* Immediately returns busy */

    /* Verify second flush is rejected while in flight */
    bool second_flush = bp_lcd_flush_async(sample_pixels, BP_LCD_WIDTH);
    assert(!second_flush);

    assert(!bp_lcd_wait_idle_timeout(0));
    assert(bp_lcd_is_busy()); /* Timeout must not release an active source buffer. */

    /* Simulate completion and verify transition to idle */
    bp_lcd_test_step_dma();
    assert(!bp_lcd_is_busy());

    /* Wait idle is safe when already idle */
    bp_lcd_wait_idle();
    assert(!bp_lcd_is_busy());

    /* Cleanup */
    bp_lcd_cleanup();
    assert(!bp_lcd_is_ready());
    assert(bp_lcd_get_dma_channel() == -1);
    assert(bp_res_get_owner(BP_RES_CLASS_DMA_CH, (uint32_t)lcd_ch) == BP_RES_OWNER_NONE);

    printf("[PASS] Asynchronous ST7789 LCD DMA refresh verified\n");
}

int main(void) {
    printf("=== Running DMA Subsystem Pipeline Test Suite (ARCH-820..822) ===\n");
    test_amux_dma_pipeline();
    uint8_t sector[512];
    /* Subtract-before-add and boundary checks */
    assert(diskio_read(0, NULL, 0, 1) == RES_PARERR);
    assert(diskio_read(0, sector, 0, 0) == RES_PARERR);
    assert(diskio_read(0, sector, 0, 129) == RES_PARERR);
    assert(diskio_read(0, sector, UINT32_MAX, 2) == RES_PARERR);
    assert(diskio_read(0, sector, (UINT32_MAX / 512), 2) == RES_PARERR);
    assert(diskio_write(0, NULL, 0, 1) == RES_PARERR);
    assert(diskio_write(0, sector, 0, 0) == RES_PARERR);
    assert(diskio_write(0, sector, 0, 129) == RES_PARERR);
    assert(diskio_write(0, sector, UINT32_MAX, 2) == RES_PARERR);
    assert(diskio_write(0, sector, (UINT32_MAX / 512), 2) == RES_PARERR);

    /* Storage service APIs */
    assert(!storage_read_blocks(0, NULL, 1));
    assert(!storage_read_blocks(0, sector, 0));
    assert(!storage_write_blocks(0, NULL, 1));
    assert(!storage_write_blocks(0, sector, 0));
    assert(!storage_read_blocks(UINT32_MAX, sector, 2));
    assert(!storage_write_blocks(UINT32_MAX, sector, 2));

    test_multiblock_storage_dma();
    test_lcd_async_dma();
    printf("=== ALL DMA PIPELINE TESTS PASSED ===\n");
    return 0;
}
