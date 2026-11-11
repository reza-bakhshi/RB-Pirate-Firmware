# Storage Subsystem Architecture

> Developer guide to persistent storage in RB-Pirate: FatFS abstraction, onboard SPI flash, TF card hotplug, and Core 0 ownership.

---

## 1. Storage Architecture Overview

The storage service runs on **Core 0** under `src/services/bp_storage.c`; MSC callbacks also run on Core 0:
- **Block-device serialization**: MSC and service storage access are serialized. The control pump skips nested USB/MSC callbacks while a block transfer holds its lock.
- **Hot-Plug Detection**: Card insertion and removal are monitored via debounced analog card detect sensing and published as system events.
- **Guarded Unmount**: Unmounting checks for open file handles and active USB MSC transactions to prevent filesystem corruption.

```
+--------------------------------------------------------------+
|                    CORE 0 STORAGE SUBSYSTEM                  |
+--------------------------------------------------------------+
|  [FatFS Wrapper]   <--->   [Block Device Driver (diskio.c)]  |
|         ^                               ^                    |
|         |                               |                    |
|  [Configuration]             [Guarded Mount/Unmount]         |
|  [RPC Job Streaming]         [SPI Flash / TF Card Hotplug]   |
+--------------------------------------------------------------+
```

---

## 2. Supported Storage Media

1. **Onboard SPI Flash**: Dedicated high-speed SPI NOR flash formatted with FAT16/FAT32 for configuration scripts and macro files.
2. **TF / MicroSD Card**: Hot-pluggable SD card interface supporting FAT12, FAT16, FAT32, and exFAT.

---

## 3. Configuration Persistence

Configuration settings (`rbconfig.rb`) use checked staging and backup recovery:
1. Write JSON to `rbconfig.tmp`; require complete writes and successful sync and close.
2. Move an existing primary to `rbconfig.bak` before committing the staged file.
3. Rename the staged file to the primary. On failure, attempt restoration and retain the backup.
4. At boot, try primary, then backup, then staging, followed by legacy names.

This is a recoverable FAT update sequence, not an atomic filesystem transaction. A power
failure can still damage FAT metadata. A failed or short write never authorizes replacement
of the previous primary. Host tests inject write, short-write, sync, close and rename errors.
