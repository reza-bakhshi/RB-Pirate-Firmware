# Storage Subsystem Architecture

> Developer guide to persistent storage in RB-Pirate: FatFS abstraction, onboard SPI flash, TF card hotplug, and Core 0 ownership.

---

## 1. Storage Architecture Overview

All filesystem operations are centralized on **Core 0 (Control Plane)** under `src/services/bp_storage.c`:
- **Single-Core FatFS Execution**: Core 1 never calls FatFS (`f_*`) or low-level block device functions directly.
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

Configuration settings (`bpconfig.bp`) are persisted through a crash-safe staging mechanism:
1. New configuration JSON is written to a temporary staging file (`staging.bp`).
2. Staging data is synced to disk via `f_sync()`.
3. The staging file is atomically renamed to replace the target configuration file.
4. If a crash or power cut occurs during save, the previous valid configuration remains intact.
