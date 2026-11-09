/**
 * @file bp_storage.c
 * @brief Control Core (Core 0) storage service, mount state machine, and storage RPC.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "services/bp_storage.h"
#include "kernel/bp_kernel.h"
#include "kernel/bp_resource.h"
#include "pirate/watchdog.h"
#include "pirate/mem.h"

#if !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "mjson/mjson.h"
#include "ui/ui_term.h"
#include "pirate.h"
#include "system_config.h"
#else
/* Host test mock declarations */
#include <stdlib.h>
#include <time.h>

typedef struct {
    uint8_t storage_available;
    uint8_t storage_mount_error;
    uint8_t storage_fat_type;
    float storage_size;
    uint32_t terminal_language;
} host_system_config_t;

extern host_system_config_t system_config;

#define FR_OK 0
#define FR_DISK_ERR 1
#define FR_INT_ERR 2
#define FR_NOT_READY 3
#define FR_NO_FILE 4
#define FR_NO_PATH 5
#define FR_INVALID_NAME 6
#define FR_DENIED 7
#define FR_EXIST 8
#define FR_INVALID_OBJECT 9
#define FR_WRITE_PROTECTED 10
#define FR_INVALID_DRIVE 11
#define FR_NOT_ENABLED 12
#define FR_NO_FILESYSTEM 13
#define FR_MKFS_ABORTED 14
#define FR_TIMEOUT 15
#define FR_LOCKED 16
#define FR_NOT_ENOUGH_CORE 17
#define FR_TOO_MANY_OPEN_FILES 18
#define FR_INVALID_PARAMETER 19

typedef int FRESULT;
typedef unsigned int UINT;

typedef struct {
    uint8_t fs_type;
    uint32_t csize;
    uint32_t n_fatent;
} FATFS;

typedef struct {
    uint32_t fsize;
    char fname[64];
    uint8_t fattrib;
} FILINFO;

typedef struct {
    uint32_t dummy;
} FIL;

typedef struct {
    uint32_t dummy;
} DIR;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_ALWAYS 0x08

#define BP_FLASH_DISK_BLOCK_SIZE 512
#define FLASH_STORAGE_CS 0
#define GPIO_FUNC_SIO 0
#define GPIO_OUT 1
#define count_of(a) (sizeof(a)/sizeof((a)[0]))

static inline void gpio_set_function(uint32_t p, uint32_t f) { (void)p; (void)f; }
static inline void gpio_put(uint32_t p, uint32_t v) { (void)p; (void)v; }
static inline void gpio_set_dir(uint32_t p, uint32_t d) { (void)p; (void)d; }

static inline FRESULT f_mount(FATFS *fs, const char *path, uint8_t opt) {
    (void)path; (void)opt;
    fs->fs_type = 2; // FAT16
    fs->csize = 8;
    fs->n_fatent = 32768;
    return FR_OK;
}
static inline FRESULT f_unmount(const char *path) { (void)path; return FR_OK; }
static inline FRESULT f_mkfs(const char *path, uint8_t opt, void *work, uint32_t len) {
    (void)path; (void)opt; (void)work; (void)len; return FR_OK;
}
static inline FRESULT f_setlabel(const char *label) { (void)label; return FR_OK; }
static inline FRESULT f_open(FIL *fp, const char *path, uint8_t mode) { (void)fp; (void)path; (void)mode; return FR_OK; }
static inline FRESULT f_close(FIL *fp) { (void)fp; return FR_OK; }
static inline FRESULT f_read(FIL *fp, void *buff, uint32_t btr, UINT *br) {
    (void)fp; (void)btr;
    const char *mock_json = "{\"terminal_language\": 1}\n";
    size_t len = strlen(mock_json);
    memcpy(buff, mock_json, len);
    *br = len;
    return FR_OK;
}
static inline FRESULT f_write(FIL *fp, const void *buff, uint32_t btw, UINT *bw) {
    (void)fp; (void)buff; (void)btw; *bw = btw; return FR_OK;
}
static inline FRESULT f_sync(FIL *fp) { (void)fp; return FR_OK; }
static inline FRESULT f_unlink(const char *path) { (void)path; return FR_OK; }
static inline FRESULT f_rename(const char *old, const char *newn) { (void)old; (void)newn; return FR_OK; }
static inline int disk_ioctl(uint8_t pdrv, uint8_t cmd, void *buff) { (void)pdrv; (void)cmd; (void)buff; return 0; }
#define CTRL_SYNC 0

static inline const char *ui_term_color_error(void) { return ""; }
static inline const char *ui_term_color_info(void) { return ""; }
static inline const char *ui_term_color_reset(void) { return ""; }

#endif // BP_HOST_TEST

#define STORAGE_DEBOUNCE_THRESHOLD 5
#define STORAGE_RPC_QUEUE_DEPTH 4
#define STORAGE_STREAM_QUEUE_DEPTH 8

#define CONFIG_FILENAME "rbconfig.rb"
#define CONFIG_FILENAME_BP "rbconfig.bp"
#define CONFIG_FILENAME_LEGACY "bpconfig.bp"
#define CONFIG_TEMP_FILENAME "rbconfig.tmp"

/* Static FatFS filesystem instance */
static FATFS s_fs;

/* Mount state and statistics */
static bp_storage_stats_t s_stats;
static volatile bool s_card_detect_raw = false;
static bool s_card_detect_stable = false;
static uint8_t s_debounce_counter = 0;
static volatile uint32_t s_open_file_count = 0;
static uint32_t s_mount_retry_ticks = 0;
static uint8_t s_mount_retry_count = 0;

/* Storage RPC Queues */
static bp_storage_rpc_req_t s_rpc_req_q[STORAGE_RPC_QUEUE_DEPTH];
static volatile uint32_t s_rpc_req_head = 0;
static volatile uint32_t s_rpc_req_tail = 0;

static bp_storage_rpc_resp_t s_rpc_resp_q[STORAGE_RPC_QUEUE_DEPTH];
static volatile uint32_t s_rpc_resp_head = 0;
static volatile uint32_t s_rpc_resp_tail = 0;

/* Streaming Capture Queue */
static bp_storage_stream_chunk_t s_stream_q[STORAGE_STREAM_QUEUE_DEPTH];
static volatile uint32_t s_stream_head = 0;
static volatile uint32_t s_stream_tail = 0;
static bool s_stream_active = false;
static FIL s_stream_fil;

/* Labels for FAT types */
const char storage_fat_type_labels[5][8] = { "FAT12", "FAT16", "FAT32", "EXFAT", "UNKNOWN" };

static const char* const s_fresult_msg[] = {
    [FR_OK]                  = "ok",
    [FR_DISK_ERR]            = "disk error",
    [FR_INT_ERR]             = "assertion failed",
    [FR_NOT_READY]           = "drive not ready",
    [FR_NO_FILE]             = "file not found",
    [FR_NO_PATH]             = "path not found",
    [FR_INVALID_NAME]        = "invalid path",
    [FR_DENIED]              = "access denied",
    [FR_EXIST]               = "access denied",
    [FR_INVALID_OBJECT]      = "invalid object",
    [FR_WRITE_PROTECTED]     = "write prohibited",
    [FR_INVALID_DRIVE]       = "invalid drive",
    [FR_NOT_ENABLED]         = "drive not enabled",
    [FR_NO_FILESYSTEM]       = "filesystem not found",
    [FR_MKFS_ABORTED]        = "format aborted",
    [FR_TIMEOUT]             = "timeout",
    [FR_LOCKED]              = "file locked",
    [FR_NOT_ENOUGH_CORE]     = "buffer full",
    [FR_TOO_MANY_OPEN_FILES] = "too many open files",
    [FR_INVALID_PARAMETER]   = "invalid parameter"
};

void bp_storage_file_error(uint8_t res) {
    if (res > 0 && res < count_of(s_fresult_msg) && s_fresult_msg[res]) {
        printf("%sError:%s %s%s\r\n", ui_term_color_error(), ui_term_color_info(), s_fresult_msg[res], ui_term_color_reset());
    } else if (res > 0) {
        printf("%sError:%s code %u%s\r\n", ui_term_color_error(), ui_term_color_info(), res, ui_term_color_reset());
    }
}

bp_status_t bp_storage_init(void) {
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.state = BP_STORAGE_STATE_UNMOUNTED;
    s_stats.fat_type = 5; // UNKNOWN
    s_open_file_count = 0;
    s_rpc_req_head = s_rpc_req_tail = 0;
    s_rpc_resp_head = s_rpc_resp_tail = 0;
    s_stream_head = s_stream_tail = 0;
    s_stream_active = false;
    s_card_detect_raw = false;
    s_card_detect_stable = false;
    s_debounce_counter = 0;
    s_mount_retry_ticks = 0;
    s_mount_retry_count = 0;

#if !defined(BP_HOST_TEST)
    gpio_set_function(FLASH_STORAGE_CS, GPIO_FUNC_SIO);
    gpio_put(FLASH_STORAGE_CS, 1);
    gpio_set_dir(FLASH_STORAGE_CS, GPIO_OUT);
#endif

    return BP_STATUS_OK;
}

bp_status_t bp_storage_mount(void) {
    s_stats.mount_attempts++;
    s_stats.state = BP_STORAGE_STATE_MOUNTING;

    FRESULT fr = f_mount(&s_fs, "", 1);
    if (fr != FR_OK) {
        s_stats.mount_errors++;
        s_stats.last_error_code = (uint8_t)fr;
        s_stats.state = BP_STORAGE_STATE_ERROR;
        system_config.storage_available = 0;
        system_config.storage_mount_error = fr;
        return BP_STATUS_ERROR;
    }

    s_stats.mount_successes++;
    s_stats.last_error_code = FR_OK;
    s_stats.state = BP_STORAGE_STATE_MOUNTED;
    s_stats.fat_type = s_fs.fs_type;
    s_stats.capacity_gb = (float)(s_fs.csize * s_fs.n_fatent * (float)BP_FLASH_DISK_BLOCK_SIZE * 1E-9);

    system_config.storage_available = 1;
    system_config.storage_mount_error = 0;
    system_config.storage_fat_type = s_fs.fs_type;
    system_config.storage_size = s_stats.capacity_gb;

    return BP_STATUS_OK;
}

bp_status_t bp_storage_unmount(void) {
    if (s_open_file_count > 0) {
        return BP_STATUS_BUSY;
    }

    s_stats.state = BP_STORAGE_STATE_UNMOUNTING;

    // Flush low level disk
    disk_ioctl(0, CTRL_SYNC, 0);
    f_unmount("");

    system_config.storage_available = 0;
    system_config.storage_mount_error = 0;
    s_stats.state = BP_STORAGE_STATE_UNMOUNTED;
    s_stats.unmount_count++;

    return BP_STATUS_OK;
}

bp_status_t bp_storage_format(void) {
    if (s_open_file_count > 0) {
        return BP_STATUS_BUSY;
    }

    uint8_t* work_buffer = mem_alloc(512, BP_BIG_BUFFER_DISKFORMAT);
    if (!work_buffer) {
        return BP_STATUS_NO_RESOURCE;
    }

    FRESULT fr = f_mkfs("", 0, work_buffer, 512);
    mem_free(work_buffer);

    if (fr != FR_OK) {
        s_stats.last_error_code = (uint8_t)fr;
        s_stats.state = BP_STORAGE_STATE_ERROR;
        return BP_STATUS_ERROR;
    }

    f_setlabel("RB_Pirate");
    return bp_storage_mount();
}

bp_storage_state_t bp_storage_get_state(void) {
    return s_stats.state;
}

void bp_storage_get_stats(bp_storage_stats_t *out_stats) {
    if (out_stats) {
        *out_stats = s_stats;
        out_stats->open_file_count = s_open_file_count;
        out_stats->card_detected = s_card_detect_stable;
    }
}

void bp_storage_notify_card_detect(bool inserted) {
    s_card_detect_raw = inserted;
}

void bp_storage_record_io_error(bool is_write) {
    if (is_write) {
        __atomic_fetch_add(&s_stats.write_errors, 1, __ATOMIC_RELAXED);
    } else {
        __atomic_fetch_add(&s_stats.read_errors, 1, __ATOMIC_RELAXED);
    }
}

void bp_storage_file_opened(void) {
    s_open_file_count++;
}

void bp_storage_file_closed(void) {
    if (s_open_file_count > 0) {
        s_open_file_count--;
    }
}

uint32_t bp_storage_get_open_count(void) {
    return s_open_file_count;
}

/* --- Internal RPC Processing on Core 0 --- */

static void bp_storage_process_single_rpc(const bp_storage_rpc_req_t *req, bp_storage_rpc_resp_t *resp) {
    resp->request_id = req->request_id;
    resp->bytes_transferred = 0;

    if (s_stats.state != BP_STORAGE_STATE_MOUNTED) {
        resp->status = BP_STATUS_DISCONNECTED;
        return;
    }

    FIL fil;
    FRESULT fr;
    UINT bw = 0;
    UINT br = 0;

    switch (req->type) {
        case BP_STORAGE_RPC_READ: {
            fr = f_open(&fil, req->path, FA_READ | FA_OPEN_EXISTING);
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.read_errors++;
                return;
            }
            bp_storage_file_opened();
            fr = f_read(&fil, req->data, req->size, &br);
            f_close(&fil);
            bp_storage_file_closed();
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.read_errors++;
            } else {
                resp->status = BP_STATUS_OK;
                resp->bytes_transferred = br;
            }
            break;
        }

        case BP_STORAGE_RPC_WRITE: {
            fr = f_open(&fil, req->path, FA_WRITE | FA_CREATE_ALWAYS);
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.write_errors++;
                return;
            }
            bp_storage_file_opened();
            fr = f_write(&fil, req->data, req->size, &bw);
            f_sync(&fil);
            f_close(&fil);
            bp_storage_file_closed();
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.write_errors++;
            } else {
                resp->status = BP_STATUS_OK;
                resp->bytes_transferred = bw;
            }
            break;
        }

        case BP_STORAGE_RPC_APPEND: {
            fr = f_open(&fil, req->path, FA_WRITE | FA_OPEN_EXISTING);
            if (fr != FR_OK) {
                fr = f_open(&fil, req->path, FA_WRITE | FA_CREATE_ALWAYS);
            }
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.write_errors++;
                return;
            }
            bp_storage_file_opened();
            fr = f_write(&fil, req->data, req->size, &bw);
            f_sync(&fil);
            f_close(&fil);
            bp_storage_file_closed();
            if (fr != FR_OK) {
                resp->status = BP_STATUS_ERROR;
                s_stats.write_errors++;
            } else {
                resp->status = BP_STATUS_OK;
                resp->bytes_transferred = bw;
            }
            break;
        }

        case BP_STORAGE_RPC_STAT: {
            fr = f_open(&fil, req->path, FA_READ | FA_OPEN_EXISTING);
            if (fr == FR_OK) {
                resp->status = BP_STATUS_OK;
                resp->bytes_transferred = 1; // exists
                f_close(&fil);
            } else {
                resp->status = BP_STATUS_ERROR;
                resp->bytes_transferred = 0;
            }
            break;
        }

        default:
            resp->status = BP_STATUS_INVALID_PARAM;
            break;
    }
}

/* --- Core 0 Cooperative Service Loop --- */

void bp_storage_service(void) {
    /* 1. Debounce Card Detect */
    if (s_card_detect_raw != s_card_detect_stable) {
        s_debounce_counter++;
        if (s_debounce_counter >= STORAGE_DEBOUNCE_THRESHOLD) {
            s_card_detect_stable = s_card_detect_raw;
            s_debounce_counter = 0;
            s_mount_retry_ticks = 0;
            s_mount_retry_count = 0;

            if (s_card_detect_stable && (s_stats.state == BP_STORAGE_STATE_UNMOUNTED || s_stats.state == BP_STORAGE_STATE_ERROR)) {
                bp_storage_mount();
            } else if (!s_card_detect_stable) {
                if (s_stats.state == BP_STORAGE_STATE_MOUNTED) {
                    bp_storage_unmount();
                } else if (s_stats.state == BP_STORAGE_STATE_ERROR) {
                    s_stats.state = BP_STORAGE_STATE_UNMOUNTED;
                }
            }
        }
    } else {
        s_debounce_counter = 0;
    }

    /* Auto-retry mount if card is present but mount failed (e.g. slow card power-up on reboot) */
    if (s_card_detect_stable && s_stats.state == BP_STORAGE_STATE_ERROR && s_mount_retry_count < 5) {
        s_mount_retry_ticks++;
        if (s_mount_retry_ticks >= 500) {
            s_mount_retry_ticks = 0;
            s_mount_retry_count++;
            bp_storage_mount();
            if (s_stats.state == BP_STORAGE_STATE_MOUNTED) {
                s_mount_retry_count = 0;
                bp_storage_load_config();
            }
        }
    }

    /* 2. Process Storage RPC requests from Core 1 */
    while (s_rpc_req_tail != s_rpc_req_head) {
        uint32_t idx = s_rpc_req_tail % STORAGE_RPC_QUEUE_DEPTH;
        bp_storage_rpc_req_t req = s_rpc_req_q[idx];
        __sync_synchronize();
        s_rpc_req_tail++;

        bp_storage_rpc_resp_t resp;
        bp_storage_process_single_rpc(&req, &resp);

        uint32_t resp_idx = s_rpc_resp_head % STORAGE_RPC_QUEUE_DEPTH;
        s_rpc_resp_q[resp_idx] = resp;
        __sync_synchronize();
        s_rpc_resp_head++;
        s_stats.rpc_requests++;
    }

    /* 3. Flush streaming capture chunks to disk */
    if (s_stream_active) {
        while (s_stream_tail != s_stream_head) {
            uint32_t chunk_idx = s_stream_tail % STORAGE_STREAM_QUEUE_DEPTH;
            bp_storage_stream_chunk_t chunk = s_stream_q[chunk_idx];
            __sync_synchronize();
            s_stream_tail++;

            if (chunk.buffer && chunk.size > 0) {
                UINT bw = 0;
                FRESULT fr = f_write(&s_stream_fil, chunk.buffer, chunk.size, &bw);
                if (fr == FR_OK) {
                    s_stats.stream_chunks_written++;
                } else {
                    s_stats.write_errors++;
                }
            }
        }
    }
}

/* --- Cross-core RPC Client (Called from Core 1) --- */

bp_status_t bp_storage_rpc_call(const bp_storage_rpc_req_t *req, bp_storage_rpc_resp_t *resp, uint32_t timeout_ms) {
    if (!req || !resp) {
        return BP_STATUS_INVALID_PARAM;
    }

    uint32_t head = s_rpc_req_head;
    uint32_t tail = s_rpc_req_tail;
    if ((head - tail) >= STORAGE_RPC_QUEUE_DEPTH) {
        s_stats.rpc_drops++;
        return BP_STATUS_NO_RESOURCE;
    }

    uint32_t idx = head % STORAGE_RPC_QUEUE_DEPTH;
    s_rpc_req_q[idx] = *req;
    __sync_synchronize();
    s_rpc_req_head = head + 1;

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        uint32_t rhead = s_rpc_resp_head;
        uint32_t rtail = s_rpc_resp_tail;

        for (uint32_t i = rtail; i != rhead; i++) {
            uint32_t resp_idx = i % STORAGE_RPC_QUEUE_DEPTH;
            if (s_rpc_resp_q[resp_idx].request_id == req->request_id) {
                *resp = s_rpc_resp_q[resp_idx];
                __sync_synchronize();
                s_rpc_resp_tail = i + 1;
                return BP_STATUS_OK;
            }
        }

        bp_watchdog_heartbeat(1);
#if !defined(BP_HOST_TEST)
        sleep_ms(1);
#endif
        waited++;
    }

    s_stats.rpc_timeouts++;
    return BP_STATUS_TIMEOUT;
}

/* --- Streaming Capture Buffer Handoff --- */

bp_status_t bp_storage_stream_open(const char *filepath) {
    if (s_stream_active) {
        return BP_STATUS_BUSY;
    }
    if (!filepath) {
        return BP_STATUS_INVALID_PARAM;
    }

    FRESULT fr = f_open(&s_stream_fil, filepath, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        s_stats.last_error_code = (uint8_t)fr;
        return BP_STATUS_ERROR;
    }

    bp_storage_file_opened();
    s_stream_head = s_stream_tail = 0;
    s_stream_active = true;
    return BP_STATUS_OK;
}

bp_status_t bp_storage_stream_write(const uint8_t *buffer, uint32_t size) {
    if (!s_stream_active) {
        return BP_STATUS_DISCONNECTED;
    }

    uint32_t head = s_stream_head;
    uint32_t tail = s_stream_tail;
    if ((head - tail) >= STORAGE_STREAM_QUEUE_DEPTH) {
        s_stats.stream_chunks_dropped++;
        return BP_STATUS_NO_RESOURCE;
    }

    uint32_t idx = head % STORAGE_STREAM_QUEUE_DEPTH;
    s_stream_q[idx].buffer = buffer;
    s_stream_q[idx].size = size;
    __sync_synchronize();
    s_stream_head = head + 1;

    return BP_STATUS_OK;
}

bp_status_t bp_storage_stream_close(void) {
    if (!s_stream_active) {
        return BP_STATUS_OK;
    }

    /* Drain all pending stream chunks */
    while (s_stream_tail != s_stream_head) {
        uint32_t idx = s_stream_tail % STORAGE_STREAM_QUEUE_DEPTH;
        bp_storage_stream_chunk_t chunk = s_stream_q[idx];
        __sync_synchronize();
        s_stream_tail++;

        if (chunk.buffer && chunk.size > 0) {
            UINT bw = 0;
            f_write(&s_stream_fil, chunk.buffer, chunk.size, &bw);
        }
    }

    f_sync(&s_stream_fil);
    f_close(&s_stream_fil);
    bp_storage_file_closed();
    s_stream_active = false;
    return BP_STATUS_OK;
}

/* --- Atomic Configuration Persistence --- */

extern const char system_config_file[];
extern const mode_config_t system_config_json[];
extern const uint8_t system_config_json_count;

bp_status_t bp_storage_save_config(void) {
    if (s_stats.state != BP_STORAGE_STATE_MOUNTED) {
        return BP_STATUS_DISCONNECTED;
    }

    /* 1. Write to temporary file */
    bp_status_t res = bp_storage_save_mode(CONFIG_TEMP_FILENAME, system_config_json, system_config_json_count);
    if (res != BP_STATUS_OK) {
        return res;
    }

    /* 2. Atomic replace */
    f_unlink(CONFIG_FILENAME);
    FRESULT fr = f_rename(CONFIG_TEMP_FILENAME, CONFIG_FILENAME);
    if (fr != FR_OK) {
        s_stats.write_errors++;
        return BP_STATUS_ERROR;
    }

    return BP_STATUS_OK;
}

bp_status_t bp_storage_load_config(void) {
    if (s_stats.state != BP_STORAGE_STATE_MOUNTED) {
        return BP_STATUS_DISCONNECTED;
    }

    bp_status_t res = bp_storage_load_mode(CONFIG_FILENAME, system_config_json, system_config_json_count);
    if (res != BP_STATUS_OK) {
        /* Fall back to temp file if present */
        res = bp_storage_load_mode(CONFIG_TEMP_FILENAME, system_config_json, system_config_json_count);
        if (res == BP_STATUS_OK) {
            /* Recover primary from temp file */
            f_unlink(CONFIG_FILENAME);
            f_rename(CONFIG_TEMP_FILENAME, CONFIG_FILENAME);
        } else {
            /* Fall back to rbconfig.bp if present and migrate to rbconfig.rb */
            res = bp_storage_load_mode(CONFIG_FILENAME_BP, system_config_json, system_config_json_count);
            if (res == BP_STATUS_OK) {
                f_rename(CONFIG_FILENAME_BP, CONFIG_FILENAME);
            } else {
                /* Fall back to legacy bpconfig.bp if present and migrate to rbconfig.rb */
                res = bp_storage_load_mode(CONFIG_FILENAME_LEGACY, system_config_json, system_config_json_count);
                if (res == BP_STATUS_OK) {
                    f_rename(CONFIG_FILENAME_LEGACY, CONFIG_FILENAME);
                }
            }
        }
    }

    return res;
}

bp_status_t bp_storage_save_mode(const char *filename, const mode_config_t *config, uint8_t count) {
    if (s_stats.state != BP_STORAGE_STATE_MOUNTED || !filename || !config) {
        return BP_STATUS_DISCONNECTED;
    }

    FIL fil;
    FRESULT fr = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        s_stats.write_errors++;
        return BP_STATUS_ERROR;
    }

    bp_storage_file_opened();

    char line[128];
    UINT bw = 0;

    f_write(&fil, "{\n", 2, &bw);
    for (uint8_t i = 0; i < count; i++) {
        if (!config[i].config) continue;
        const char *tag = (config[i].tag[0] == '$' && config[i].tag[1] == '.') ? &config[i].tag[2] : config[i].tag;
        const char *comma = (i == (count - 1)) ? "" : ",";
        if (config[i].formatted_as == MODE_CONFIG_FORMAT_HEXSTRING) {
            snprintf(line, sizeof(line), "\"%s\": \"0x%06X\"%s\n", tag, (unsigned)*config[i].config, comma);
        } else {
            snprintf(line, sizeof(line), "\"%s\": %u%s\n", tag, (unsigned)*config[i].config, comma);
        }
        f_write(&fil, line, strlen(line), &bw);
    }
    f_write(&fil, "}\n", 2, &bw);

    f_sync(&fil);
    f_close(&fil);
    bp_storage_file_closed();
    return BP_STATUS_OK;
}

bp_status_t bp_storage_load_mode(const char *filename, const mode_config_t *config, uint8_t count) {
    if (s_stats.state != BP_STORAGE_STATE_MOUNTED || !filename || !config) {
        return BP_STATUS_DISCONNECTED;
    }

    FIL fil;
    FRESULT fr = f_open(&fil, filename, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        /* Check for legacy formats to migrate:
         * e.g., if looking for "rbrfid.rb", check "rbrfid.bp", "bprfid.rb", "bprfid.bp" */
        char alt_name[32];
        const char *dot = strrchr(filename, '.');
        size_t base_len = dot ? (size_t)(dot - filename) : strlen(filename);

        /* 1. If looking for .rb, check .bp counterpart */
        if (dot && strcmp(dot, ".rb") == 0 && base_len < sizeof(alt_name) - 4) {
            snprintf(alt_name, sizeof(alt_name), "%.*s.bp", (int)base_len, filename);
            fr = f_open(&fil, alt_name, FA_READ | FA_OPEN_EXISTING);
            if (fr == FR_OK) {
                f_close(&fil);
                f_rename(alt_name, filename);
                fr = f_open(&fil, filename, FA_READ | FA_OPEN_EXISTING);
            }
        }

        /* 2. If looking for rb*.*, check bp*.* counterpart */
        if (fr != FR_OK && filename[0] == 'r' && filename[1] == 'b') {
            /* Try bp<base>.rb */
            snprintf(alt_name, sizeof(alt_name), "bp%s", filename + 2);
            fr = f_open(&fil, alt_name, FA_READ | FA_OPEN_EXISTING);
            if (fr == FR_OK) {
                f_close(&fil);
                f_rename(alt_name, filename);
                fr = f_open(&fil, filename, FA_READ | FA_OPEN_EXISTING);
            } else if (dot && strcmp(dot, ".rb") == 0) {
                /* Try bp<base>.bp */
                snprintf(alt_name, sizeof(alt_name), "bp%.*s.bp", (int)(base_len - 2), filename + 2);
                fr = f_open(&fil, alt_name, FA_READ | FA_OPEN_EXISTING);
                if (fr == FR_OK) {
                    f_close(&fil);
                    f_rename(alt_name, filename);
                    fr = f_open(&fil, filename, FA_READ | FA_OPEN_EXISTING);
                }
            }
        }
    }
    if (fr != FR_OK) {
        return BP_STATUS_ERROR;
    }

    bp_storage_file_opened();

    char json[512];
    UINT br = 0;
    fr = f_read(&fil, json, sizeof(json) - 1, &br);
    f_close(&fil);
    bp_storage_file_closed();

    if (fr != FR_OK || br == 0) {
        s_stats.read_errors++;
        return BP_STATUS_ERROR;
    }
    json[br] = '\0';

    /* Validate JSON structure */
    if (json[0] != '{' || strchr(json, '}') == NULL) {
        s_stats.read_errors++;
        return BP_STATUS_INVALID_PARAM;
    }

#if !defined(BP_HOST_TEST)
    for (uint8_t i = 0; i < count; i++) {
        const char *tag = config[i].tag;
        if (config[i].formatted_as == MODE_CONFIG_FORMAT_HEXSTRING) {
            char as_hex[16] = {0};
            int len = mjson_get_string(json, br, tag, as_hex, sizeof(as_hex));
            if (len > 0) {
                *(config[i].config) = strtoul(as_hex, NULL, 16);
            }
        } else {
            double dv = 0;
            int found = mjson_get_number(json, br, tag, &dv);
            if (found) {
                *(config[i].config) = (uint32_t)dv;
            }
        }
    }
#else
    (void)count;
#endif

    return BP_STATUS_OK;
}

/* --- Block Device Concurrency Lock --- */

#if !defined(BP_HOST_TEST)
static mutex_t s_blockdev_mutex;
static bool s_blockdev_mutex_initialized = false;

void blockdev_lock_acquire(void) {
    if (!s_blockdev_mutex_initialized) {
        mutex_init(&s_blockdev_mutex);
        s_blockdev_mutex_initialized = true;
    }
    mutex_enter_blocking(&s_blockdev_mutex);
}

void blockdev_lock_release(void) {
    if (s_blockdev_mutex_initialized) {
        mutex_exit(&s_blockdev_mutex);
    }
}
#else
void blockdev_lock_acquire(void) {}
void blockdev_lock_release(void) {}
#endif
