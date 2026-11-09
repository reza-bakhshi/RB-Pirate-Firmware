#ifndef TUSB_H
#define TUSB_H

#include <stdint.h>
#include <stdbool.h>

#define CFG_TUD_MSC 1

enum {
    SCSI_SENSE_NONE            = 0x00,
    SCSI_SENSE_RECOVERED_ERROR = 0x01,
    SCSI_SENSE_NOT_READY       = 0x02,
    SCSI_SENSE_MEDIUM_ERROR    = 0x03,
    SCSI_SENSE_HARDWARE_ERROR  = 0x04,
    SCSI_SENSE_ILLEGAL_REQUEST = 0x05,
    SCSI_SENSE_UNIT_ATTENTION  = 0x06,
    SCSI_SENSE_DATA_PROTECT    = 0x07,
    SCSI_SENSE_FIRMWARE_ERROR  = 0x08,
    SCSI_SENSE_ABORTED_COMMAND = 0x0b,
};

bool tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code, uint8_t add_sense_qualifier);

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
bool tud_msc_test_unit_ready_cb(uint8_t lun);
bool tud_msc_is_writable_cb(uint8_t lun);
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size);
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]);

#endif // TUSB_H
