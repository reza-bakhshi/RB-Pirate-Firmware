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
#include "fatfs/diskio.h"
#include "fatfs/tf_card.h"

/* Mock system config */
struct _system_config system_config;

/* Mock spin lock & core num */
unsigned int get_core_num(void) {
    return 0;
}

static spin_lock_t s_mock_spin_lock = 0;
uint32_t spin_lock_blocking(spin_lock_t *lock) { (void)lock; return 0; }
void spin_unlock(spin_lock_t *lock, uint32_t save) { (void)lock; (void)save; }
uint spin_lock_claim_unused(bool required) { (void)required; return 1; }
spin_lock_t* spin_lock_init(uint lock_num) { (void)lock_num; return &s_mock_spin_lock; }
void spi_busy_wait_internal(bool enable, const char *file, int line) {
    (void)enable; (void)file; (void)line;
}

void blockdev_lock_acquire(void) {}
void blockdev_lock_release(void) {}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv; (void)cmd; (void)buff;
    return RES_OK;
}

// -------------------------------------------------------------------------
// Helper: Construct CSD v1.0 (SDSC)
// -------------------------------------------------------------------------
static void build_csd_v1(uint8_t csd[16], uint8_t read_bl_len, uint16_t c_size, uint8_t c_size_mult, bool wp) {
    memset(csd, 0, 16);
    // csd_struct = 0 in bits [7:6] of csd[0]
    csd[0] = 0x00;
    // read_bl_len in bits [3:0] of csd[5]
    csd[5] = (uint8_t)(read_bl_len & 0x0F);
    // c_size (12 bits) across csd[6..8]:
    // csd[6][1:0] = c_size[11:10]
    // csd[7][7:0] = c_size[9:2]
    // csd[8][7:6] = c_size[1:0]
    csd[6] |= (uint8_t)((c_size >> 10) & 0x03);
    csd[7] = (uint8_t)((c_size >> 2) & 0xFF);
    csd[8] |= (uint8_t)((c_size & 0x03) << 6);
    // c_size_mult (3 bits) across csd[9..10]:
    // csd[9][1:0] = c_size_mult[2:1]
    // csd[10][7]  = c_size_mult[0]
    csd[9] |= (uint8_t)((c_size_mult >> 1) & 0x03);
    csd[10] |= (uint8_t)((c_size_mult & 0x01) << 7);
    if (wp) {
        csd[14] |= 0x10; // WP bit
    }
}

// -------------------------------------------------------------------------
// Helper: Construct CSD v2.0 (SDHC/SDXC)
// -------------------------------------------------------------------------
static void build_csd_v2(uint8_t csd[16], uint32_t c_size, bool wp) {
    memset(csd, 0, 16);
    // csd_struct = 1 in bits [7:6] of csd[0] -> 0x40
    csd[0] = 0x40;
    // c_size (22 bits) across csd[7..9]:
    // csd[7][5:0] = c_size[21:16]
    // csd[8][7:0] = c_size[15:8]
    // csd[9][7:0] = c_size[7:0]
    csd[7] = (uint8_t)((c_size >> 16) & 0x3F);
    csd[8] = (uint8_t)((c_size >> 8) & 0xFF);
    csd[9] = (uint8_t)(c_size & 0xFF);
    if (wp) {
        csd[14] |= 0x10;
    }
}

// -------------------------------------------------------------------------
// Test 1: Null and Zero / Floating Bus Check
// -------------------------------------------------------------------------
static void test_csd_null_and_bus_faults(void) {
    printf("[TEST] CSD NULL, all-zero, and all-0xFF detection...\n");
    tf_card_metadata_t meta;

    assert(!tf_card_parse_csd(NULL, &meta));
    uint8_t csd[16];
    assert(!tf_card_parse_csd(csd, NULL));

    // All zero (unpowered or missing card)
    memset(csd, 0x00, 16);
    assert(!tf_card_parse_csd(csd, &meta));
    assert(!meta.valid);

    // All 0xFF (floating/pulled-up bus)
    memset(csd, 0xFF, 16);
    assert(!tf_card_parse_csd(csd, &meta));
    assert(!meta.valid);

    printf("[PASS] NULL, all-zero, and all-0xFF inputs properly rejected\n");
}

// -------------------------------------------------------------------------
// Test 2: CSD v1.0 Parsing (SDSC <= 2GB)
// -------------------------------------------------------------------------
static void test_csd_v1_sdsc(void) {
    printf("[TEST] CSD v1.0 (SDSC byte-addressed) parsing...\n");
    uint8_t csd[16];
    tf_card_metadata_t meta;

    // 1. Valid 512 MB SDSC card:
    // read_bl_len = 9 (512 bytes), c_size = 999, c_size_mult = 7 (MULT = 512)
    // sectors = (999 + 1) * 2^(7 + 2 + 9 - 9) = 1000 * 512 = 512,000 sectors (250 MB)
    build_csd_v1(csd, 9, 999, 7, false);
    assert(tf_card_parse_csd(csd, &meta));
    assert(meta.valid);
    assert(meta.csd_version == 1);
    assert(meta.addr_mode == TF_CARD_ADDR_BYTE);
    assert(meta.sector_size == 512);
    assert(meta.sector_count == 512000ULL);
    assert(!meta.write_protected);

    // 2. Maximum valid 2GB SDSC card:
    // read_bl_len = 10 (1024 bytes), c_size = 4095, c_size_mult = 7 (MULT = 512)
    // shift = 7 + 2 + 10 - 9 = 10
    // sectors = (4095 + 1) << 10 = 4096 * 1024 = 4,194,304 sectors (exactly 2 GB)
    build_csd_v1(csd, 10, 4095, 7, true);
    assert(tf_card_parse_csd(csd, &meta));
    assert(meta.valid);
    assert(meta.sector_count == 4194304ULL);
    assert(meta.write_protected);

    // 3. Invalid read_bl_len (< 9 or > 11)
    build_csd_v1(csd, 8, 1000, 4, false);
    assert(!tf_card_parse_csd(csd, &meta));

    build_csd_v1(csd, 12, 1000, 4, false);
    assert(!tf_card_parse_csd(csd, &meta));

    // 4. Over 2GB capacity violation for SDSC (> 4,194,304 sectors)
    build_csd_v1(csd, 11, 4095, 7, false);
    // shift = 7 + 2 + 11 - 9 = 11 -> 4096 << 11 = 8,388,608 sectors (4GB, illegal for SDSC)
    assert(!tf_card_parse_csd(csd, &meta));

    printf("[PASS] CSD v1.0 SDSC parsing and boundary validation verified\n");
}

// -------------------------------------------------------------------------
// Test 3: CSD v2.0 Parsing & 2^32 Sector Boundary Without Truncation
// -------------------------------------------------------------------------
static void test_csd_v2_sdhc_sdxc_and_2tb_boundary(void) {
    printf("[TEST] CSD v2.0 SDHC/SDXC parsing & 2^32-sector boundary without truncation...\n");
    uint8_t csd[16];
    tf_card_metadata_t meta;

    // 1. Standard 4 GB SDHC card:
    // sectors = (c_size + 1) * 1024
    // c_size = 3800 -> (3800 + 1) * 1024 = 3,892,224 sectors (~1.99 GB)
    build_csd_v2(csd, 3800, false);
    assert(tf_card_parse_csd(csd, &meta));
    assert(meta.valid);
    assert(meta.csd_version == 2);
    assert(meta.addr_mode == TF_CARD_ADDR_BLOCK);
    assert(meta.sector_count == (3800ULL + 1ULL) * 1024ULL);
    assert(!meta.write_protected);

    // 2. 32 GB SDHC card:
    // c_size = 62463 -> (62463 + 1) * 1024 = 63,963,136 sectors (~32.7 GB)
    build_csd_v2(csd, 62463, true);
    assert(tf_card_parse_csd(csd, &meta));
    assert(meta.valid);
    assert(meta.sector_count == 63963136ULL);
    assert(meta.write_protected);

    // 3. Maximum 2 TB SDXC Card (2^32 sectors):
    // Max 22-bit c_size = 0x3FFFFF (4,194,303).
    // sectors = (0x3FFFFF + 1) * 1024 = 4,194,304 * 1024 = 4,294,967,296 sectors (2^32)!
    build_csd_v2(csd, 0x3FFFFF, false);
    assert(tf_card_parse_csd(csd, &meta));
    assert(meta.valid);
    assert(meta.sector_count == 4294967296ULL);
    // Crucial check: verify 32-bit truncation would wrap to 0, but uint64_t holds full value!
    assert((uint32_t)meta.sector_count == 0);
    assert(meta.sector_count > 0xFFFFFFFFULL);

    // 4. Invalid CSD v2: c_size exceeding 22-bit field (0x400000)
    // Note: since build_csd_v2 masks with 0x3F for byte 7, test manually setting > 0x3FFFFF
    memset(csd, 0, 16);
    csd[0] = 0x40; // CSD v2
    csd[7] = 0x40; // Bit 6 set (invalid in 22-bit field)
    assert(!tf_card_parse_csd(csd, &meta));

    // 5. Invalid CSD v2: capacity < 2MB (sectors < 4096)
    build_csd_v2(csd, 1, false); // sectors = 2 * 1024 = 2048 < 4096
    assert(!tf_card_parse_csd(csd, &meta));

    printf("[PASS] CSD v2.0 parsing and 2^32-sector boundary without truncation verified\n");
}

// -------------------------------------------------------------------------
// Test 4: Unsupported CSD v3.0 (SDUC) and Reserved Versions
// -------------------------------------------------------------------------
static void test_csd_unsupported_versions(void) {
    printf("[TEST] Unsupported CSD v3.0 (SDUC) and reserved structure rejection...\n");
    uint8_t csd[16];
    tf_card_metadata_t meta;

    // CSD v3.0 (SDUC, bits [7:6] == 2 -> 0x80)
    memset(csd, 0x55, 16);
    csd[0] = 0x80;
    assert(!tf_card_parse_csd(csd, &meta));
    assert(!meta.valid);
    assert(meta.csd_version == 3);
    assert(meta.addr_mode == TF_CARD_ADDR_UNSUPPORTED);

    // Reserved CSD (bits [7:6] == 3 -> 0xC0)
    memset(csd, 0x55, 16);
    csd[0] = 0xC0;
    assert(!tf_card_parse_csd(csd, &meta));
    assert(!meta.valid);
    assert(meta.addr_mode == TF_CARD_ADDR_UNSUPPORTED);

    printf("[PASS] CSD v3.0 and reserved versions cleanly rejected\n");
}

// -------------------------------------------------------------------------
// Test 5: Media Generation Lifecycle and Metadata Invalidation
// -------------------------------------------------------------------------
static void test_media_generation_and_invalidation(void) {
    printf("[TEST] Media generation lifecycle and metadata invalidation...\n");
    tf_card_metadata_t meta;

    // Start clean
    tf_card_invalidate_metadata();
    assert(tf_card_get_media_generation() == 0);
    assert(!tf_card_get_metadata(&meta));

    // Verify invalidation clears all fields
    memset(&meta, 0xAA, sizeof(meta));
    tf_card_get_metadata(&meta);
    assert(meta.sector_count == 0);
    assert(meta.valid == false);

    printf("[PASS] Media generation lifecycle and invalidation verified\n");
}

int main(void) {
    printf("=== Running Storage Capacity & CSD Metadata Parsing Test Suite ===\n");
    test_csd_null_and_bus_faults();
    test_csd_v1_sdsc();
    test_csd_v2_sdhc_sdxc_and_2tb_boundary();
    test_csd_unsupported_versions();
    test_media_generation_and_invalidation();
    printf("=== ALL STORAGE CAPACITY & CSD METADATA TESTS PASSED ===\n");
    return 0;
}
