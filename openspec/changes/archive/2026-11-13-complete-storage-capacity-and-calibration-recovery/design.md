# Design: Complete storage capacity and calibration recovery

## Context and current evidence

Bounds checks reject arithmetic overflow but do not establish requests lie within the actual inserted card. Calibration recovers a missing primary, but a present corrupt primary still prevents valid backup recovery; plausible coefficient corruption lacks a persisted integrity check.

Scope: Core 0 storage service owns card capacity/media generation, FatFS handles, calibration parsing and durable replacement. Core 1 uses typed storage requests and receives bounded completion statuses.

## Core ownership, state and failure behavior

Obtain capacity from validated card metadata at mount/initialization, represent sector count without truncating a 2^32-sector device and reject unsupported address widths explicitly. For nonzero count, validate count <= capacity and lba <= capacity - count before command generation; validate the last byte-addressed sector against the command argument width. Validate API count/byte-size conversion independently. Publish capacity with a media generation; invalidate both on removal or failed initialization so queued work cannot run against replacement media. USB MSC capacity and READ/WRITE range checks use the same authoritative metadata. Completion must report actual bytes, short-write errors and the media generation that completed it.

Try calibration candidates in a deterministic order: canonical primary, its backup, supported legacy primary/backup paths, then factory defaults. A candidate must be fully read within the bounded input size, syntactically complete, schema-compatible and valid before application; reject trailing garbage, duplicate fields, nonfinite/fractional numbers, overflow, truncated JSON and unsupported future versions. A corrupt primary must not stop backup search. Retain or quarantine invalid content for diagnosis without applying it.

Add a schema version and persisted corruption-detection checksum over a documented canonical representation of all coefficients/version. Checksum is integrity detection, not authentication. Define legacy eight-field migration explicitly, test valid legacy data and rewrite only after successful validation. Use temp/sync/close plus backup replacement and recover after every interruption point. All file access must move behind the storage service, including calibration file-open accounting and unmount exclusion. Do not erase the sole known-valid backup when a current primary is missing or invalid. Return failure on short write, sync, close or rename errors; never claim universal power-loss atomicity for FAT.

## Resources and rollback

Use existing storage SPI/DMA leases and block-device serialization. File handles and media-generation tokens remain owned by Core 0. Timed-out requests must not retain pointers to released worker buffers.

All start/failure/stop paths need an explicit rollback table before implementation. Document queue/message types, producer/consumer core, capacity, ownership-transfer point and late-reply lifetime whenever a new cross-core request is introduced. Completion callbacks must not access a caller buffer after its documented release point.

## Static memory and lifetime

Reuse the existing 512-byte calibration JSON input and bounded formatting buffer. Parse into one candidate structure at a time. Add fixed capacity/generation metadata only with measured byte deltas. Avoid full-file duplicate buffers or a heap JSON tree.

## Prerequisites and exclusions

FAT journaling guarantees, cryptographic authenticity without a key, repairing arbitrary filesystem damage, or treating SD-internal write caches as durable without evidence.

No capability is complete merely because configuration accepts it. Hardware gates require independent evidence. New engineering choices and measured timing must be reflected in the corresponding delta scenarios before sign-off.

## Shared verification gates

All new runtime code must use static storage or bounded pools; no malloc/calloc/realloc/free/new/delete or heap-backed STL. RP2350 static main RAM must remain <=389120 bytes, including actual linked reservations measured by the existing memory guard. Baseline in the audit: 388504 bytes, leaving 616 bytes under the ceiling and 135784 bytes main-RAM headroom; scratch stacks are separately reserved at 4 KB/core. These are historical measurements, not a budget approval for future growth. Measure actual byte deltas and stack high-water after each task group.

Core 1 must not print, call FatFS or USB. Use typed bounded queues and published snapshots across cores; queue-full, disconnect, SD hotplug, timeout, cancel and watchdog behavior must be exercised where relevant. No busy wait may be unbounded. No hardware resource claims may bypass bp_res. Startup/teardown must restore leases, GPIO functions, buffer directions, IRQ handlers, clocks and PSU protection.

After each implementation task group, run `cmake --build build_rp2350 --target bus_pirate6`, the named focused suite and `bash tests/test_memory_model.sh`; record flash/RAM and warnings. Final gates: `bash tests/run_all.sh`, a separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`, zero-warning supported-platform builds and physical acceptance. Provide a reproducible wrapper/preset for optimized sanitizer flags rather than assuming scripts' existing -O1 runs satisfy -O2. Record each skipped case; a hardware skip cannot satisfy release acceptance. A build named build_rp2040 is not an RP2040 gate without verified platform identity.

## Proposed persisted calibration schema

Use a versioned object with `schema_version` equal to integer 1, the existing eight integer coefficients, and a `checksum_crc32` field containing exactly eight hexadecimal digits. Define canonical checksum input independently of JSON whitespace/key order: one uint32 little-endian schema version followed, in order, by uint16 little-endian `vout_gain_ppt`, int16 two's-complement little-endian `vout_offset_mv`, uint16 `isense_gain_ppt`, int16 `isense_offset_ua`, uint16 `vdac_gain_ppt`, int16 `vdac_offset_mv`, uint16 `idac_gain_ppt`, int16 `idac_offset_ma`. This is 20 bytes. Use CRC-32/ISO-HDLC (reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF). Do not checksum structure padding or floating-point values. Confirm this schema against any existing externally consumed files before implementation; update the delta if compatibility requires a different encoding.

Reject duplicates of known fields and unknown schema versions. For version 1, missing checksum or fields is invalid. Legacy files with no schema/checksum may be accepted only if they have exactly the eight known unique fields, complete JSON and existing physical coefficient bounds; report legacy status, then migrate only after successful parsing and a durable save. A file specifying only one of schema/checksum must not bypass validation by being treated as legacy. Checksum detects accidental modification; it does not protect against malicious editing. Keep calibration data separate from runtime magic, validity flags and in-memory checksum fields.
