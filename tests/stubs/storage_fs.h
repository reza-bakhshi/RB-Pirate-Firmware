#ifndef TEST_STORAGE_FS_H
#define TEST_STORAGE_FS_H
/* Host test mock declarations */
#include <stdlib.h>
#include <time.h>

#ifndef _SYSTEM_CONFIG_H
typedef struct {
    uint8_t storage_available;
    uint8_t storage_mount_error;
    uint8_t storage_fat_type;
    float storage_size;
    uint32_t terminal_language;
} host_system_config_t;

extern host_system_config_t system_config;
#endif

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
    unsigned slot;
    unsigned pos;
} FIL;

typedef struct {
    uint32_t dummy;
} DIR;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_APPEND 0x30

#define BP_FLASH_DISK_BLOCK_SIZE 512
#ifndef FLASH_STORAGE_CS
#define FLASH_STORAGE_CS 0
#endif
#ifndef GPIO_FUNC_SIO
#define GPIO_FUNC_SIO 0
#endif
#ifndef GPIO_OUT
#define GPIO_OUT 1
#endif
#ifndef count_of
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
#endif

#ifndef _PICO_STDLIB_H
static inline void gpio_set_function(uint32_t p, uint32_t f) { (void)p; (void)f; }
static inline void gpio_put(uint32_t p, uint32_t v) { (void)p; (void)v; }
static inline void gpio_set_dir(uint32_t p, uint32_t d) { (void)p; (void)d; }
#endif

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
static inline FRESULT f_mkdir(const char *path) { (void)path; return FR_OK; }
/* Stateful filesystem fake: production service logic is compiled unchanged. */
struct mock_file_storage_t { char name[64]; unsigned size; unsigned char data[4096]; };
__attribute__((weak)) struct mock_file_storage_t mock_files[16];
__attribute__((weak)) int mock_write_error, mock_short_write, mock_sync_error, mock_close_error, mock_rename_error;

static inline int mock_find(const char *path) {
    for (int i = 0; i < 16; i++) if (!strcmp(mock_files[i].name, path)) return i;
    return -1;
}
static inline FRESULT f_open(FIL *fp, const char *path, uint8_t mode) {
    int i = mock_find(path);
    if (i < 0) {
        if (!(mode & (FA_CREATE_ALWAYS | FA_OPEN_APPEND))) return FR_NO_FILE;
        for (i = 0; i < 16 && mock_files[i].name[0]; i++);
        if (i == 16) return FR_DENIED;
        snprintf(mock_files[i].name, 64, "%s", path);
        mock_files[i].size = 0;
    }
    if (mode & FA_CREATE_ALWAYS) mock_files[i].size = 0;
    fp->slot = i;
    fp->pos = (mode & FA_OPEN_APPEND) == FA_OPEN_APPEND ? mock_files[i].size : 0;
    return FR_OK;
}
static inline FRESULT f_close(FIL *fp) { (void)fp; return mock_close_error ? FR_DISK_ERR : FR_OK; }
static inline FRESULT f_lseek(FIL *fp, uint32_t pos) {
    if (pos > 4096) return FR_DENIED;
    fp->pos = pos; return FR_OK;
}
static inline FRESULT f_read(FIL *fp, void *buf, uint32_t n, UINT *br) {
    unsigned left = fp->pos < mock_files[fp->slot].size ? mock_files[fp->slot].size - fp->pos : 0;
    *br = n < left ? n : left;
    memcpy(buf, mock_files[fp->slot].data + fp->pos, *br); fp->pos += *br; return FR_OK;
}
static inline FRESULT f_write(FIL *fp, const void *buf, uint32_t n, UINT *bw) {
    *bw = 0;
    if (mock_write_error) return FR_DISK_ERR;
    if (mock_short_write && n) n--;
    if (n > 4096 - fp->pos) n = 4096 - fp->pos;
    memcpy(mock_files[fp->slot].data + fp->pos, buf, n); fp->pos += n; *bw = n;
    if (fp->pos > mock_files[fp->slot].size) mock_files[fp->slot].size = fp->pos;
    return FR_OK;
}
static inline FRESULT f_sync(FIL *fp) { (void)fp; return mock_sync_error ? FR_DISK_ERR : FR_OK; }
static inline FRESULT f_unlink(const char *path) {
    int i = mock_find(path); if (i < 0) return FR_NO_FILE;
    memset(&mock_files[i], 0, sizeof(mock_files[i])); return FR_OK;
}
static inline FRESULT f_stat(const char *path, FILINFO *info) {
    int i = mock_find(path); if (i < 0) return FR_NO_FILE;
    info->fsize = mock_files[i].size; return FR_OK;
}
static inline FRESULT f_rename(const char *old, const char *newn) {
    if (mock_rename_error) return FR_DISK_ERR;
    int i = mock_find(old); if (i < 0) return FR_NO_FILE;
    if (mock_find(newn) >= 0) return FR_EXIST;
    snprintf(mock_files[i].name, 64, "%s", newn); return FR_OK;
}
static inline int disk_ioctl(uint8_t pdrv, uint8_t cmd, void *buff) { (void)pdrv; (void)cmd; (void)buff; return 0; }
#define CTRL_SYNC 0

static inline const char *ui_term_color_error(void) { return ""; }
static inline const char *ui_term_color_info(void) { return ""; }
static inline const char *ui_term_color_reset(void) { return ""; }


#endif
