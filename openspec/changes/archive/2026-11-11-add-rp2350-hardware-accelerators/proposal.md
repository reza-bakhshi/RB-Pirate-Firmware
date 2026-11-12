# Proposal

## Why
The RP2350B microcontroller includes dedicated hardware accelerators—notably a hardware SHA-256 engine and a ROSC/TRNG true random number generator—that are currently unutilized in firmware. At the same time, firmware flashing, flash chip dumping, and file transfers over TF card lack hardware-accelerated integrity verification, while RF and IR jamming sequences rely on standard pseudo-random number generators (`rand()`). Furthermore, the logic analyzer buffer is constrained to a legacy 128 KB allocation despite the RP2350B possessing 520 KB of SRAM. Integrating these hardware capabilities significantly accelerates cryptographic verification, improves signal randomness, and doubles logic capture depth.

## What Changes
- **Hardware SHA-256 Acceleration**:
  - Add `src/commands/global/cmd_sha256.c` providing the `sha256` CLI command utilizing `pico/sha256.h`.
  - Support high-speed SHA-256 calculation on files stored on the TF card (`sha256 <filename>`), raw memory buffers, and dumped SPI flash memory regions.
- **Hardware TRNG Entropy Integration**:
  - Implement a hardware entropy source in `src/pirate/trng.c` utilizing RP2350 ROSC/TRNG hardware.
  - Integrate hardware randomness into SubGHz and Infrared carrier/timing noise jamming routines.
  - Add `rand` / `entropy` command to dump random words and test entropy on demand.
- **256 KB Logic Analyzer Buffer Depth**:
  - Expand `LA_BUFFER_SIZE` from 128 KB to 256 KB on RP2350 targets while preserving the static memory footprint below 380 KB total SRAM.

## Capabilities

### New Capabilities
- `hardware-sha256`: Provides high-speed hardware-accelerated SHA-256 checksumming of storage files, flash blocks, and memory buffers.
- `trng-entropy`: Provides true random number generation via RP2350 hardware entropy for cryptographic, jamming, and fuzzing applications.

### Modified Capabilities
- `binary-modes`: Expand logic analyzer sample depth to 256 KB.

## Impact
- Core 0 and Core 1: `src/commands/global/cmd_sha256.c`, `src/pirate/trng.c`, `src/mode/subghz.c`, `src/commands/infrared/ir_jam.c`, `src/binmode/logicanalyzer.h`.
- Adds zero heap allocation (uses streaming 512-byte block hashing for files).
- Doubles logic capture sample depth without compromising system memory limits.
