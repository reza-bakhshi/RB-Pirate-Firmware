#define _POSIX_C_SOURCE 199309L
#define __dmb() __sync_synchronize()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "tusb.h"
#include "services/bp_storage.h"
#include "msc_disk.h"
#include "fatfs/diskio.h"

/* System config mock */
typedef struct {
    uint8_t storage_available;
    uint8_t storage_mount_error;
    uint8_t storage_fat_type;
    float storage_size;
    uint32_t terminal_language;
} host_system_config_t;

host_system_config_t system_config;

const char system_config_file[] = "rbconfig.rb";
const mode_config_t system_config_json[1] = {
    {"$.terminal_language", &system_config.terminal_language, MODE_CONFIG_FORMAT_DECIMAL},
};
const uint8_t system_config_json_count = 1;

void* mem_alloc(size_t size, uint32_t type) {
    (void)type;
    return malloc(size);
}

void mem_free(void* ptr) {
    free(ptr);
}

/* Track last sense set */
static uint8_t s_last_sense_key = 0;
static uint8_t s_last_asc = 0;
static uint8_t s_last_ascq = 0;

bool tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code, uint8_t add_sense_qualifier) {
    (void)lun;
    s_last_sense_key = sense_key;
    s_last_asc = add_sense_code;
    s_last_ascq = add_sense_qualifier;
    return true;
}

/* Mock disk_read / disk_write / disk_ioctl */
static bool s_mock_disk_read_fail = false;
static bool s_mock_disk_write_fail = false;
static uint8_t s_mock_disk[32][512];

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (s_mock_disk_read_fail) {
        return RES_ERROR;
    }
    if (sector + count > 32) {
        return RES_PARERR;
    }
    memcpy(buff, &s_mock_disk[sector], count * 512);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (s_mock_disk_write_fail) {
        return RES_ERROR;
    }
    if (sector + count > 32) {
        return RES_PARERR;
    }
    memcpy(&s_mock_disk[sector], buff, count * 512);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)pdrv;
    (void)cmd;
    (void)buff;
    return RES_OK;
}

/* Mock watchdog */
void bp_watchdog_heartbeat(uint8_t core_id) {
    (void)core_id;
}

void bp_watchdog_service(void) {
}

/* Mock sleep_ms */
void sleep_ms(uint32_t ms) {
    (void)ms;
}

static void test_demo_disk_bounds_protection(void) {
    printf("[TEST] Demo disk fallback LBA bounds protection...\n");
    msc_disk_reset();
    bp_storage_init();
    system_config.storage_available = 0;

    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    /* Valid read of boot sector (LBA 0) */
    int32_t ret = tud_msc_read10_cb(0, 0, 0, buf, 512);
    assert(ret == 512);
    assert(buf[510] == 0x55 && buf[511] == 0xAA); /* FAT boot signature */

    /* Valid read of Readme sector (LBA 3) */
    ret = tud_msc_read10_cb(0, 3, 0, buf, 512);
    assert(ret == 512);
    assert(strstr((char*)buf, "No storage mounted.") != NULL);

    /* Valid read of last block (LBA 15) */
    ret = tud_msc_read10_cb(0, 15, 0, buf, 512);
    assert(ret == 512);

    /* Out of bounds LBA >= 16 must fail with -1 and sense 0x05 / 0x21 */
    s_last_sense_key = 0;
    s_last_asc = 0;
    ret = tud_msc_read10_cb(0, 16, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_ILLEGAL_REQUEST);
    assert(s_last_asc == 0x21); /* LOGICAL BLOCK ADDRESS OUT OF RANGE */

    ret = tud_msc_read10_cb(0, 100, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_ILLEGAL_REQUEST);
    assert(s_last_asc == 0x21);

    /* Offset + bufsize > 512 must fail */
    ret = tud_msc_read10_cb(0, 0, 500, buf, 20);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_ILLEGAL_REQUEST);
    assert(s_last_asc == 0x21);

    /* Valid sub-block read */
    ret = tud_msc_read10_cb(0, 0, 10, buf, 100);
    assert(ret == 100);

    printf("[PASS] Demo disk bounds protection verified\n");
}

static void test_demo_disk_write_protection(void) {
    printf("[TEST] Demo disk write protection...\n");
    msc_disk_reset();
    bp_storage_init();
    system_config.storage_available = 0;

    /* Is writable callback must report false for demo disk */
    assert(tud_msc_is_writable_cb(0) == false);

    uint8_t buf[512];
    memset(buf, 0xAA, sizeof(buf));

    /* WRITE10 must be rejected with -1 and DATA_PROTECT sense (0x07 / 0x27) */
    s_last_sense_key = 0;
    s_last_asc = 0;
    int32_t ret = tud_msc_write10_cb(0, 0, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_DATA_PROTECT);
    assert(s_last_asc == 0x27); /* WRITE PROTECTED */

    ret = tud_msc_write10_cb(0, 5, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_DATA_PROTECT);
    assert(s_last_asc == 0x27);

    printf("[PASS] Demo disk write protection verified\n");
}

static void test_physical_media_io_and_error_handling(void) {
    printf("[TEST] Physical media I/O and error accounting...\n");
    msc_disk_reset();
    bp_storage_init();
    system_config.storage_available = 1;

    bp_storage_stats_t stats;
    bp_storage_get_stats(&stats);
    uint32_t initial_read_err = stats.read_errors;
    uint32_t initial_write_err = stats.write_errors;

    uint8_t buf[512];
    memset(buf, 0x55, sizeof(buf));

    /* Successful write */
    s_mock_disk_write_fail = false;
    int32_t ret = tud_msc_write10_cb(0, 1, 0, buf, 512);
    assert(ret == 512);

    /* Successful read */
    s_mock_disk_read_fail = false;
    memset(buf, 0, sizeof(buf));
    ret = tud_msc_read10_cb(0, 1, 0, buf, 512);
    assert(ret == 512);
    assert(buf[0] == 0x55 && buf[511] == 0x55);

    /* Failing read */
    s_mock_disk_read_fail = true;
    s_last_sense_key = 0;
    s_last_asc = 0;
    ret = tud_msc_read10_cb(0, 1, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_MEDIUM_ERROR);
    assert(s_last_asc == 0x11); /* UNRECOVERED READ ERROR */

    bp_storage_get_stats(&stats);
    assert(stats.read_errors == initial_read_err + 1);

    /* Failing write */
    s_mock_disk_write_fail = true;
    s_last_sense_key = 0;
    s_last_asc = 0;
    ret = tud_msc_write10_cb(0, 1, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_MEDIUM_ERROR);
    assert(s_last_asc == 0x0c); /* WRITE ERROR */

    bp_storage_get_stats(&stats);
    assert(stats.write_errors == initial_write_err + 1);

    /* Prepare read-only rejects write */
    s_mock_disk_write_fail = false;
    prepare_usbmsdrive_readonly();
    assert(tud_msc_is_writable_cb(0) == false);

    ret = tud_msc_write10_cb(0, 1, 0, buf, 512);
    assert(ret == -1);
    assert(s_last_sense_key == SCSI_SENSE_DATA_PROTECT);
    assert(s_last_asc == 0x27);

    make_usbmsdrive_writable();
    assert(tud_msc_is_writable_cb(0) == true);

    printf("[PASS] Physical media I/O and telemetry error accounting verified\n");
}

static void test_hotplug_state_machine(void) {
    printf("[TEST] Hotplug state machine & SCSI sense reporting...\n");
    msc_disk_reset();
    bp_storage_init();

    /* Step 1: Start with storage available */
    system_config.storage_available = 1;
    bool ready = tud_msc_test_unit_ready_cb(0);
    assert(ready == true);
    assert(s_last_sense_key == 0x00);

    /* Step 2: Storage is removed (1 -> 0) */
    system_config.storage_available = 0;
    s_last_sense_key = 0;
    s_last_asc = 0;
    ready = tud_msc_test_unit_ready_cb(0);
    assert(ready == false);
    assert(s_last_sense_key == SCSI_SENSE_NOT_READY);
    assert(s_last_asc == 0x3a); /* MEDIUM NOT PRESENT */

    /* Second check while absent still reports NOT READY */
    ready = tud_msc_test_unit_ready_cb(0);
    assert(ready == false);
    assert(s_last_sense_key == SCSI_SENSE_NOT_READY);
    assert(s_last_asc == 0x3a);

    /* Step 3: Storage is re-inserted (0 -> 1) */
    system_config.storage_available = 1;
    s_last_sense_key = 0;
    s_last_asc = 0;

    /* First check upon insertion must return false with UNIT ATTENTION 0x28 */
    ready = tud_msc_test_unit_ready_cb(0);
    assert(ready == false);
    assert(s_last_sense_key == SCSI_SENSE_UNIT_ATTENTION);
    assert(s_last_asc == 0x28); /* MEDIUM MAY HAVE CHANGED */

    /* Subsequent check must transition to ready */
    ready = tud_msc_test_unit_ready_cb(0);
    assert(ready == true);
    assert(s_last_sense_key == 0x00);

    printf("[PASS] Hotplug edge detection and SCSI sense reporting verified\n");
}

int main(void) {
    printf("=== Running MSC Storage Unit Tests ===\n");
    test_demo_disk_bounds_protection();
    test_demo_disk_write_protection();
    test_physical_media_io_and_error_handling();
    test_hotplug_state_machine();
    printf("ALL MSC STORAGE TESTS PASSED!\n");
    return 0;
}
