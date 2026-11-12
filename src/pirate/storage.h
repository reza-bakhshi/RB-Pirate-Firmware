/**
 * @file storage.h
 * @brief Flash storage and FAT filesystem interface.
 * @details Provides FatFS-based filesystem access to onboard flash storage or
 *          TF/microSD card (depending on hardware revision). Supports config
 *          file persistence, binary logging, and file operations.
 */

#ifndef PIRATE__STORAGE_H
#define PIRATE__STORAGE_H

/**
 * @brief Initialize storage hardware (CS pin, SPI).
 * @note Must be called before mount/unmount operations.
 */
void storage_init(void);

/**
 * @brief Detect storage media presence (for TF card variants).
 * @return true if storage detected and ready, false otherwise
 */
bool storage_detect(void);

/**
 * @brief Mount the FAT filesystem.
 * @return FatFS result code (FR_OK on success)
 * @note Updates system_config with mount status and filesystem info.
 */
uint8_t storage_mount(void);

/**
 * @brief Unmount filesystem and sync pending writes.
 */
void storage_unmount(void);

/**
 * @brief Format storage media with FAT filesystem.
 * @return FatFS result code (FR_OK on success)
 */
uint8_t storage_format(void);

/**
 * @brief Print human-readable FatFS error message.
 * @param res  FatFS result code (FRESULT)
 */
void storage_file_error(uint8_t res);

/**
 * @brief Save binary data with automatic file rollover.
 * @param data      Pointer to binary data buffer
 * @param ptr       Current write position in file
 * @param size      Size of data to write
 * @param rollover  Maximum file size before creating new file
 * @return true on success, false on error
 */
bool storage_save_binary_blob_rollover(char* data, uint32_t ptr, uint32_t size, uint32_t rollover);

/**
 * @brief Multi-block read from physical storage via DMA.
 * @param lba       Starting sector number (LBA)
 * @param buffer    Destination buffer (size >= count * 512)
 * @param count     Number of 512-byte sectors to read
 * @return true on success, false on error
 */
bool storage_read_blocks(uint32_t lba, uint8_t* buffer, uint32_t count);

/**
 * @brief Multi-block write to physical storage via DMA.
 * @param lba       Starting sector number (LBA)
 * @param buffer    Source buffer (size >= count * 512)
 * @param count     Number of 512-byte sectors to write
 * @return true on success, false on error
 */
bool storage_write_blocks(uint32_t lba, const uint8_t* buffer, uint32_t count);

/**
 * @brief Check if storage DMA channels are leased and operational.
 */
bool storage_dma_is_active(void);

/**
 * @brief Configuration value format for mode config files.
 */
typedef enum _mode_config_format {
    MODE_CONFIG_FORMAT_DECIMAL,   ///< Store as decimal integer
    MODE_CONFIG_FORMAT_HEXSTRING, ///< Store as hex string
} mode_config_format_t;

/**
 * @brief Mode configuration descriptor for file I/O.
 */
typedef struct _mode_config_t {
    char tag[30];                 ///< JSON tag name
    uint32_t* config;             ///< Pointer to config variable
    mode_config_format_t formatted_as; ///< Format type
} mode_config_t;

/**
 * @brief Load global system configuration from rbconfig.rb file.
 * @return FatFS result code (FR_OK on success)
 */
uint32_t storage_load_config(void);

/**
 * @brief Save global system configuration to rbconfig.rb file.
 * @return FatFS result code (FR_OK on success)
 */
uint32_t storage_save_config(void);

/**
 * @brief Save mode-specific configuration to file.
 * @param filename  Target filename (e.g., "rbuart.rb")
 * @param config_t  Array of mode_config_t descriptors
 * @param count     Number of config entries
 * @return FatFS result code (FR_OK on success)
 */
uint32_t storage_save_mode(const char* filename, const mode_config_t* config_t, uint8_t count);

/**
 * @brief Load mode-specific configuration from file.
 * @param filename  Source filename (e.g., "rbuart.rb")
 * @param config_t  Array of mode_config_t descriptors to populate
 * @param count     Number of config entries
 * @return FatFS result code (FR_OK on success)
 */
uint32_t storage_load_mode(const char* filename, const mode_config_t* config_t, uint8_t count);

/**
 * @brief Check if a file exists on storage.
 * @param filepath  Path to file
 * @return true if file exists, false otherwise
 */
bool storage_file_exists(const char* filepath);

/**
 * @brief List directory contents with filtering options.
 * @param location  Directory path to list
 * @param ext       File extension filter (or NULL for all)
 * @param flags     Combination of LS_* flags
 * @return true on success, false on error
 */
bool storage_ls(const char* location, const char* ext, const uint8_t flags);

/**
 * @brief FAT filesystem type label strings.
 */
extern const char storage_fat_type_labels[5][8];

/**
 * @name Directory listing flags
 * @{
 */
#define LS_FILES  0x01  ///< List files
#define LS_DIRS   0x02  ///< List directories
#define LS_SIZE   0x04  ///< Show file sizes
#define LS_SUMM   0x08  ///< Show summary statistics
#define LS_LONG   0x10  ///< Long listing: show date/time alongside size
#define LS_HIDDEN 0x20  ///< Include hidden/system files
#define LS_ALL (LS_FILES | LS_DIRS | LS_SIZE | LS_SUMM) ///< All flags enabled
/** @} */

#endif
