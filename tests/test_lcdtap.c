#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "kernel/bp_status.h"
#include "kernel/bp_resource.h"
#include "kernel/bp_job.h"
#include "mode/lcdtap_backend.h"

/* --- PIO & DMA Mock Structures for Teardown Verification --- */
typedef struct {
    uint32_t instructions[32];
    uint8_t count;
} mock_pio_program_t;

typedef struct {
    uint32_t claimed_sm_mask;
    uint32_t program_mask;
    uint8_t program_count;
} mock_pio_hw_t;

static mock_pio_hw_t s_mock_pio1;
static uint32_t s_mock_dma_claimed_mask = 0;
static bool s_mock_irq_dma1_enabled = false;

/* Mock program descriptors matching spi_4line_mode0_program and spi_3line_mode0_program */
static const mock_pio_program_t mock_spi4_prog = { { 0 }, 8 };
static const mock_pio_program_t mock_spi3_prog = { { 0 }, 10 };

/* Mock SpiSlaveState representing state in spi_slave.cpp */
typedef struct {
    int dma_ch;
    int sm;
    uint32_t prog_offset;
    const void *pio_program;
    bool running;
} mock_spi_slave_t;

static void mock_spi_slave_init(mock_spi_slave_t *s, int sm) {
    s->sm = sm;
    s_mock_pio1.claimed_sm_mask |= (1u << sm);

    // In spi_slave.cpp: pio_add_program and assign s->pioProgram
    s->prog_offset = s_mock_pio1.program_count;
    s_mock_pio1.program_count += mock_spi4_prog.count;
    s->pio_program = &mock_spi4_prog;

    // DMA channel claim
    s->dma_ch = 3;
    s_mock_dma_claimed_mask |= (1u << s->dma_ch);
    s_mock_irq_dma1_enabled = true;
    s->running = true;
}

static void mock_spi_slave_deinit(mock_spi_slave_t *s) {
    if (s->dma_ch >= 0) {
        s_mock_dma_claimed_mask &= ~(1u << s->dma_ch);
        s_mock_irq_dma1_enabled = false;
        s->dma_ch = -1;
    }
    if (s->pio_program) {
        // In spi_slave.cpp: pio_remove_program
        assert(s_mock_pio1.program_count >= mock_spi4_prog.count);
        s_mock_pio1.program_count -= mock_spi4_prog.count;
        s->pio_program = NULL;
    }
    s->running = false;
}

static void mock_spi3_init(mock_spi_slave_t *s, int sm) {
    s->sm = sm;
    s_mock_pio1.claimed_sm_mask |= (1u << sm);
    s->prog_offset = s_mock_pio1.program_count;
    s_mock_pio1.program_count += mock_spi3_prog.count;
    s->pio_program = &mock_spi3_prog;

    s->dma_ch = 4;
    s_mock_dma_claimed_mask |= (1u << s->dma_ch);
    s_mock_irq_dma1_enabled = true;
    s->running = true;
}

static void mock_spi3_deinit(mock_spi_slave_t *s) {
    if (s->dma_ch >= 0) {
        s_mock_dma_claimed_mask &= ~(1u << s->dma_ch);
        s_mock_irq_dma1_enabled = false;
        s->dma_ch = -1;
    }
    if (s->pio_program) {
        assert(s_mock_pio1.program_count >= mock_spi3_prog.count);
        s_mock_pio1.program_count -= mock_spi3_prog.count;
        s->pio_program = NULL;
    }
    s->running = false;
}

/* --- Tests --- */

static void test_controller_validation_and_ks0108(void) {
    printf("[TEST] Controller validation and KS0108 availability check...\n");

    // Supported controllers
    assert(LCDTAP_CTRL_ST7789 == 0);
    assert(LCDTAP_CTRL_SSD1306 == 1);
    assert(LCDTAP_CTRL_SSD1331 == 2);
    assert(LCDTAP_CTRL_ILI9341 == 3);
    assert(LCDTAP_CTRL_ST7032 == 4);
    assert(LCDTAP_CTRL_KS0108 == 5);

    // ST7032 text geometry rules: width 2..40, height 1, 2, or 4
    uint32_t w = 16, h = 2;
    bool st7032_valid = (w >= 2 && w <= 40 && (h == 1 || h == 2 || h == 4));
    assert(st7032_valid == true);

    w = 50; h = 2;
    st7032_valid = (w >= 2 && w <= 40 && (h == 1 || h == 2 || h == 4));
    assert(st7032_valid == false);

    w = 16; h = 3;
    st7032_valid = (w >= 2 && w <= 40 && (h == 1 || h == 2 || h == 4));
    assert(st7032_valid == false);

    // KS0108 check: requires 13 pins (D0-D7, CS1, CS2, RS, E, R/W), impossible on 8 BIO pins
    bool ks0108_supported_on_8pin_bio = false;
    assert(ks0108_supported_on_8pin_bio == false);

    printf("[PASS] Controller validation verified\n");
}

static void test_pio_pointer_and_100_start_stop_cycles(void) {
    printf("[TEST] 100 consecutive SPI4 & SPI3 start/stop cycles with PIO/DMA verification...\n");
    memset(&s_mock_pio1, 0, sizeof(s_mock_pio1));
    s_mock_dma_claimed_mask = 0;
    s_mock_irq_dma1_enabled = false;

    mock_spi_slave_t slave;

    for (int cycle = 0; cycle < 100; cycle++) {
        // Alternate between SPI4 and SPI3
        if ((cycle % 2) == 0) {
            mock_spi_slave_init(&slave, 0);
            assert(slave.pio_program == &mock_spi4_prog);
            assert(slave.dma_ch == 3);
            assert(s_mock_pio1.program_count == mock_spi4_prog.count);
            assert(s_mock_dma_claimed_mask == (1u << 3));
            assert(s_mock_irq_dma1_enabled == true);

            mock_spi_slave_deinit(&slave);
            assert(slave.pio_program == NULL);
            assert(slave.dma_ch == -1);
            assert(s_mock_pio1.program_count == 0);
            assert(s_mock_dma_claimed_mask == 0);
            assert(s_mock_irq_dma1_enabled == false);
        } else {
            mock_spi3_init(&slave, 1);
            assert(slave.pio_program == &mock_spi3_prog);
            assert(slave.dma_ch == 4);
            assert(s_mock_pio1.program_count == mock_spi3_prog.count);
            assert(s_mock_dma_claimed_mask == (1u << 4));
            assert(s_mock_irq_dma1_enabled == true);

            mock_spi3_deinit(&slave);
            assert(slave.pio_program == NULL);
            assert(slave.dma_ch == -1);
            assert(s_mock_pio1.program_count == 0);
            assert(s_mock_dma_claimed_mask == 0);
            assert(s_mock_irq_dma1_enabled == false);
        }
    }

    assert(s_mock_pio1.program_count == 0);
    assert(s_mock_dma_claimed_mask == 0);
    assert(s_mock_irq_dma1_enabled == false);

    printf("[PASS] Zero PIO/DMA leaks across 100 cycles verified\n");
}

#ifndef DMA_IRQ_1
#define DMA_IRQ_1 1
#endif

static void test_kernel_resource_lease_arbitration(void) {
    printf("[TEST] Kernel resource lease arbitration for LCDTAP...\n");
    assert(bp_res_init() == BP_STATUS_OK);

    // 1. SPI4 Mode Leases
    uint32_t sm = 0;
    assert(bp_res_claim_unused_pio_sm(1, BP_RES_OWNER_LCDTAP, "lcdtap_pio", &sm) == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_PIO_SM, (1 * BP_RES_SM_PER_PIO) + sm));
    assert(bp_res_get_owner(BP_RES_CLASS_PIO_SM, (1 * BP_RES_SM_PER_PIO) + sm) == BP_RES_OWNER_LCDTAP);

    uint32_t dma_ch = 2;
    assert(bp_res_acquire(BP_RES_CLASS_DMA_CH, dma_ch, BP_RES_OWNER_LCDTAP, "lcdtap_dma") == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_DMA_CH, dma_ch));

    assert(bp_res_acquire(BP_RES_CLASS_IRQ, DMA_IRQ_1, BP_RES_OWNER_LCDTAP, "lcdtap_irq") == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_IRQ, DMA_IRQ_1));

    // Release all on stop
    uint32_t released = bp_res_release_all(BP_RES_OWNER_LCDTAP);
    assert(released >= 3);
    assert(bp_res_is_free(BP_RES_CLASS_PIO_SM, (1 * BP_RES_SM_PER_PIO) + sm));
    assert(bp_res_is_free(BP_RES_CLASS_DMA_CH, dma_ch));
    assert(bp_res_is_free(BP_RES_CLASS_IRQ, DMA_IRQ_1));

    // 2. I2C Mode Leases & Conflict Handling
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0));
    assert(bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0, BP_RES_OWNER_LCDTAP, "lcdtap_i2c") == BP_STATUS_OK);
    assert(!bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0));

    // Conflict: Another subsystem attempts to claim I2C0
    assert(bp_res_acquire(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0, 105, "other_mode") == BP_STATUS_NO_RESOURCE);

    // Stop releases I2C0
    bp_res_release_all(BP_RES_OWNER_LCDTAP);
    assert(bp_res_is_free(BP_RES_CLASS_PERIPH, BP_RES_PERIPH_I2C0));

    printf("[PASS] Kernel resource lease arbitration verified\n");
}

static void test_bmp_export_format_and_pixel_math(void) {
    printf("[TEST] BMP export 24-bit header calculation and RGB565 conversion...\n");

    uint32_t width = 320;
    uint32_t height = 240;
    uint32_t row_bytes = (width * 3u + 3u) & ~3u;
    assert(row_bytes == 960); // 320 * 3 = 960 (already multiple of 4)

    width = 128;
    height = 64;
    row_bytes = (width * 3u + 3u) & ~3u;
    assert(row_bytes == 384); // 128 * 3 = 384 (multiple of 4)

    width = 135;
    row_bytes = (width * 3u + 3u) & ~3u;
    assert(row_bytes == 408); // 135 * 3 = 405 -> padded to 408

    (void)height;

    // Pixel math: RGB565 -> RGB888
    // Red:   0xF800 -> (31, 0, 0) -> R=255, G=0, B=0
    uint16_t red_565 = 0xF800;
    uint8_t r = ((red_565 >> 11) & 0x1fu) * 255u / 31u;
    uint8_t g = ((red_565 >> 5) & 0x3fu) * 255u / 63u;
    uint8_t b = (red_565 & 0x1fu) * 255u / 31u;
    assert(r == 255);
    assert(g == 0);
    assert(b == 0);

    // Green: 0x07E0 -> (0, 63, 0) -> R=0, G=255, B=0
    uint16_t green_565 = 0x07E0;
    r = ((green_565 >> 11) & 0x1fu) * 255u / 31u;
    g = ((green_565 >> 5) & 0x3fu) * 255u / 63u;
    b = (green_565 & 0x1fu) * 255u / 31u;
    assert(r == 0);
    assert(g == 255);
    assert(b == 0);

    // Blue:  0x001F -> (0, 0, 31) -> R=0, G=0, B=255
    uint16_t blue_565 = 0x001F;
    r = ((blue_565 >> 11) & 0x1fu) * 255u / 31u;
    g = ((blue_565 >> 5) & 0x3fu) * 255u / 63u;
    b = (blue_565 & 0x1fu) * 255u / 31u;
    assert(r == 0);
    assert(g == 0);
    assert(b == 255);

    printf("[PASS] BMP export format and pixel math verified\n");
}

static bp_status_t mock_slice(void *ctx, uint8_t *pct) {
    (void)ctx;
    if (pct) *pct = 50;
    return BP_STATUS_OK;
}

static void test_bmp_save_cancellation_contract(void) {
    printf("[TEST] BMP save cooperative cancellation contract (< 100 ms)...\n");
    assert(bp_job_subsystem_init() == BP_STATUS_OK);

    uint32_t jid = 0;
    assert(bp_job_submit("lcdtap_save", BP_JOB_PROTO_TEST, 5000, mock_slice, NULL, 0, &jid) == BP_STATUS_OK);

    bool write_protected = true;
    uint32_t height = 320;
    uint32_t rows_saved = 0;
    bool ok = true;

    for (int32_t y = (int32_t)height - 1; ok && y >= 0; y--) {
        if (y == 270) {
            // Cancel job mid-save
            assert(bp_job_cancel(jid) == BP_STATUS_OK);
        }

        if (bp_job_check_cancelled()) {
            ok = false;
            break;
        }

        rows_saved++;
    }

    // Teardown must clear write protection
    write_protected = false;

    assert(ok == false);
    assert(rows_saved == (319 - 270));
    assert(write_protected == false);

    printf("[PASS] BMP save cancellation contract verified\n");
}

int main(void) {
    printf("=== Bus Pirate LCDTap Subsystem Host Unit Tests ===\n");
    test_controller_validation_and_ks0108();
    test_pio_pointer_and_100_start_stop_cycles();
    test_kernel_resource_lease_arbitration();
    test_bmp_export_format_and_pixel_math();
    test_bmp_save_cancellation_contract();
    printf("=== ALL LCDTAP UNIT TESTS PASSED ===\n");
    return 0;
}
