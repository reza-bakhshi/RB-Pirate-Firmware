# Tasks

## 1. Hardware SHA-256 Engine
- [x] 1.1 Implement `src/commands/global/cmd_sha256.c` using Pico SDK 2.x `pico/sha256.h` with 512-byte streaming chunks
- [x] 1.2 Register `sha256` command in command dispatch table and verify checksum computation against known vectors

## 2. Hardware TRNG Entropy
- [x] 2.1 Implement `src/pirate/trng.c` reading hardware entropy registers on RP2350 via `pico/rand.h` (with host-test fallback)
- [x] 2.2 Wire `bp_trng_get_u32()` into SubGHz and Infrared noise jamming routines
- [x] 2.3 Implement `rand` command to display generated hardware random words

## 3. Scaled Capture Depth & Static RAM Budget
- [x] 3.1 Verify static buffer capacity against RP2350 SRAM layout and retain 128 KB big buffer to preserve 141 KB runtime headroom
- [x] 3.2 Verify memory model test `tests/test_memory_model.sh` passes within static limits (382,452 / 389,120 bytes)
