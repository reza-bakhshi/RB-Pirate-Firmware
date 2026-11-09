/**
 * @file storage.c
 * @brief Flash storage and FAT filesystem implementation.
 * @details Provides FatFS wrapper for persistent storage:
 *          - Onboard SPI flash (BP5 Rev10+)
 *          - TF/microSD card with hot-plug detection (BP5 Rev8/Rev9)
 *          
 *          Features:
 *          - Automatic mount/unmount on card insertion/removal
 *          - JSON-based configuration file persistence
 *          - Binary data logging with automatic file rollover
 *          - Directory listing and file operations
 *          - FAT12/FAT16/FAT32/exFAT support via FatFS
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "hardware/timer.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "pirate/bio.h"
#include "ui/ui_prompt.h"
#include "ui/ui_parse.h"
#include "ui/ui_term.h"
#include "mjson/mjson.h"
#include "pirate/storage.h"
#include "services/bp_storage.h"
#include "pirate/mem.h"
#include "ui/ui_cmdln.h"

FATFS fs; /* FatFs work area needed for each volume */
// FIL fil;			/* File object needed for each open file */
// FRESULT fr;     /* FatFs return code */
//  Size of read/write.
#define BUF_SIZE 512
// Insure 4-byte alignment.
uint32_t buf32[(BUF_SIZE + 3) / 4];
uint8_t* buf = (uint8_t*)buf32;
UINT br;
UINT bw;

void storage_file_error(uint8_t res) {
    bp_storage_file_error(res);
}

void storage_init(void) {
    bp_storage_init();
}

uint8_t storage_mount(void) {
    bp_status_t st = bp_storage_mount();
    return (st == BP_STATUS_OK) ? FR_OK : (uint8_t)system_config.storage_mount_error;
}

void storage_unmount(void) {
    bp_storage_unmount();
}

bool storage_detect(void) {
    bp_storage_service();
    return (system_config.storage_available != 0);
}

uint8_t storage_format(void) {
    bp_status_t st = bp_storage_format();
    return (st == BP_STATUS_OK) ? FR_OK : FR_DISK_ERR;
}

bool storage_save_binary_blob_rollover(char* data, uint32_t ptr, uint32_t size, uint32_t rollover) {
    FRESULT fr; /* FatFs return code */
    FIL fil;    /* File object needed for each open file */
    fr = f_open(&fil, "la.bin", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        bp_storage_file_opened();
        // middle to end
        if (ptr + size <= rollover) {
            f_write(&fil, &data[ptr], size, &bw);
        } else {
            f_write(&fil, &data[ptr], rollover - ptr, &bw);
            f_write(&fil, &data[0], size - (rollover - ptr), &bw);
        }
        fr = f_close(&fil);
        bp_storage_file_closed();
        if (fr == FR_OK) {
            return true;
        } else {
            return false;
        }
    }
    return false;
}


uint32_t storage_save_mode(const char* filename, const mode_config_t* config, uint8_t count) {
    return (bp_storage_save_mode(filename, config, count) == BP_STATUS_OK) ? 1 : 0;
}

uint32_t storage_load_mode(const char* filename, const mode_config_t* config, uint8_t count) {
    return (bp_storage_load_mode(filename, config, count) == BP_STATUS_OK) ? 1 : 0;
}
bool storage_file_exists(const char* filepath) {
    bool result = false;
    FIL fil = {0};
    FRESULT fr = f_open(&fil, filepath, FA_READ | FA_OPEN_EXISTING);
    if (fr == FR_OK) {
        bp_storage_file_opened();
        result = true;
        f_close(&fil);
        bp_storage_file_closed();
    }
    return result;
}
bool storage_ls(const char* location, const char* ext, const uint8_t flags) {
    FRESULT fr;
    DIR dir;
    FILINFO fno;
    int nfile, ndir;

    fr = f_opendir(&dir, location); /* Open the directory */
    if (fr != FR_OK) {
        return false;
    }
    bp_storage_file_opened();

    nfile = ndir = 0;

    /* ── Pass 1: directories (listed first, like ls --group-directories-first) ── */
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }
        if (!(fno.fattrib & AM_DIR)) {
            continue; /* skip files on this pass */
        }
        /* skip hidden/system unless LS_HIDDEN */
        if (!(flags & LS_HIDDEN) && (fno.fattrib & (AM_HID | AM_SYS))) {
            continue;
        }
        strlwr(fno.fname);
        if (!(flags & LS_DIRS)) {
            ndir++;
            continue;
        }
        if (flags & LS_LONG) {
            /* long format: size  date  time  name/ */
            int year  = ((fno.fdate >> 9) & 0x7F) + 1980;
            int month = (fno.fdate >> 5) & 0x0F;
            int day   = fno.fdate & 0x1F;
            int hour  = (fno.ftime >> 11) & 0x1F;
            int min   = (fno.ftime >> 5) & 0x3F;
            printf("%s       -  %s%4d-%02d-%02d %02d:%02d  %s%s/%s\r\n",
                   ui_term_color_prompt(),
                   ui_term_color_grey(),
                   year, month, day, hour, min,
                   ui_term_color_info(),
                   fno.fname,
                   ui_term_color_reset());
        } else if (flags & LS_SIZE) {
            /* legacy size mode (backward compat) */
            printf("%s       -  %s%s/%s\r\n",
                   ui_term_color_prompt(),
                   ui_term_color_info(),
                   fno.fname,
                   ui_term_color_reset());
        } else {
            /* short format: name/ */
            printf("%s%s/%s\r\n",
                   ui_term_color_info(),
                   fno.fname,
                   ui_term_color_reset());
        }
        ndir++;
    }

    /* ── Pass 2: files ── */
    f_rewinddir(&dir);
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }
        if (fno.fattrib & AM_DIR) {
            continue; /* skip dirs on this pass */
        }
        /* skip hidden/system unless LS_HIDDEN */
        if (!(flags & LS_HIDDEN) && (fno.fattrib & (AM_HID | AM_SYS))) {
            continue;
        }
        strlwr(fno.fname);
        if (ext) {
            int fname_len = strlen(fno.fname);
            int ext_len = strlen(ext);
            if (fname_len > ext_len + 1) {
                if (memcmp(fno.fname + fname_len - ext_len, ext, ext_len)) {
                    continue;
                }
            }
        }
        if (!(flags & LS_FILES)) {
            nfile++;
            continue;
        }
        if (flags & LS_LONG) {
            /* long format: size  date  time  name */
            int year  = ((fno.fdate >> 9) & 0x7F) + 1980;
            int month = (fno.fdate >> 5) & 0x0F;
            int day   = fno.fdate & 0x1F;
            int hour  = (fno.ftime >> 11) & 0x1F;
            int min   = (fno.ftime >> 5) & 0x3F;
            printf("%s%8u  %s%4d-%02d-%02d %02d:%02d  %s%s%s\r\n",
                   ui_term_color_prompt(),
                   (unsigned)fno.fsize,
                   ui_term_color_grey(),
                   year, month, day, hour, min,
                   ui_term_color_reset(),
                   fno.fname,
                   ui_term_color_reset());
        } else if (flags & LS_SIZE) {
            /* legacy size mode (backward compat) */
            printf("%s%8u  %s%s%s\r\n",
                   ui_term_color_prompt(),
                   (unsigned)fno.fsize,
                   ui_term_color_reset(),
                   fno.fname,
                   ui_term_color_reset());
        } else {
            /* short format: name only */
            printf("%s%s\r\n", fno.fname, ui_term_color_reset());
        }
        nfile++;
    }
    f_closedir(&dir);
    bp_storage_file_closed();
    if (flags & LS_SUMM) {
        printf("%s%d dirs, %d files%s\r\n", ui_term_color_info(), ndir, nfile, ui_term_color_reset());
    }
    return true;
}

const char system_config_file[] = "rbconfig.rb";

const mode_config_t system_config_json[] = {
    // clang-format off
    {"$.terminal_language",           &system_config.terminal_language,                MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.terminal_ansi_color",         &system_config.terminal_ansi_color,              MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.terminal_ansi_statusbar",     &system_config.terminal_ansi_statusbar,          MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.display_format",              &system_config.display_format,                   MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.led_effect",                  &system_config.led_effect_as_uint32,             MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.led_color",                   &system_config.led_color,                        MODE_CONFIG_FORMAT_HEXSTRING, },
    {"$.led_brightness_divisor",      &system_config.led_brightness_divisor,           MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.terminal_usb_enable",         &system_config.terminal_usb_enable,              MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.terminal_uart_enable",        &system_config.terminal_uart_enable,             MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.terminal_uart_number",        &system_config.terminal_uart_number,             MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.debug_uart_enable",           &system_config.debug_uart_enable,                MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.debug_uart_number",           &system_config.debug_uart_number,                MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.disable_usb_serial_number",   &system_config.disable_unique_usb_serial_number, MODE_CONFIG_FORMAT_DECIMAL,   },
    {"$.bpio_debug_enable",           &system_config.bpio_debug_enable,               MODE_CONFIG_FORMAT_DECIMAL,   },
    //....-....1....-....2....-....3
    // clang-format on
};

const uint8_t system_config_json_count = count_of(system_config_json);

uint32_t storage_save_config(void) {
    return (bp_storage_save_config() == BP_STATUS_OK) ? 1 : 0;
}

uint32_t storage_load_config(void) {
    return (bp_storage_load_config() == BP_STATUS_OK) ? 1 : 0;
}
