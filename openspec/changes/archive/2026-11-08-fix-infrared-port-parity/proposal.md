# Proposal: Fix Infrared Port Parity and Protocol Ambiguities

## Why

The INFRARED mode on RB-Pirate RP2350 was ported from the ESP32-Bit-Pirate project, providing raw signal captures, hand-written protocol encoders/decoders (NEC, Samsung, Sony, RC5, RC6, Panasonic, etc.), and 59 MakeHex/IRP protocol definitions. However, several critical defects and regressions prevent complete reliability and parity:

1. **Protocol Lookup Ambiguity and Shadowing**:
   Seven protocol names (`jvc`, `panasonic`, `rc5`, `rc6`, `sharp`, `denon`, `mitsubishi`) are duplicated between hand-written native encoders (indices 0..12) and MakeHex IRP table entries (indices 13..74). Because `ir_protocols_find()` performs a linear scan from index 0, the MakeHex definitions are completely shadowed and impossible to select by command-line name or one-shot flag (`send -p ...`).
2. **PIO TX Loop Counter 8-Bit Overflow**:
   In `irio_pio_tx_frame_write()` (`src/pirate/irio_pio.c`), the frame transmission loop uses `uint8_t i` to iterate over `uint16_t pairs`. If `pairs >= 256` (e.g. dense raw captures, long IR replay buffers, or extended IRP bursts), `i` overflows at 255 and wraps to 0, entering an infinite loop that locks up Core 1 or hangs transmission.
3. **Missing Timing Bounds & Zero-Duration Rejection**:
   In `irio_pio_tx_frame_write()`, `irtx_transmit()`, and protocol encoders, mark and space durations are not strictly validated against 16-bit limits (`> 0xFFFF`) or zero-duration pairs before transmission. In RP2040/RP2350 PIO assembly (`irio.pio`), pushing zero or overflowed counter values causes unintended carrier bursts, carrier lockup, or timing distortions. In `irtxrx.c`, ASCII decimal timing parsing into `uint16_t value` silently overflows when values exceed 65535, corrupting timings.
4. **Buffer Truncation in Command Handlers**:
   In `ir_setprotocol.c`, the protocol name input buffer is sized at `char name[17]`, which truncates longer protocol names such as `dishplayer_network` (18 chars), causing selection to fail silently. In `irtxrx.c`, trailing semicolon appending calculates string length after modification, leaving an uninitialized byte in the buffer.

This change (`F3`) resolves all 7 protocol name collisions, fixes the loop counter bug in `irio_pio_tx_frame_write()`, implements strict timing validation (rejecting 0 and >16-bit values), hardens buffer bounds across all IR commands, and verifies hand-written vs MakeHex protocol selection and `.ir` file replay.

## What Changes

- **Disambiguate Protocol Table Names (`src/pirate/ir_protocols.c`, `src/pirate/ir_protocols.h`)**:
  - Rename duplicated MakeHex protocol entries to explicit aliases:
    - `jvc` -> `jvc_mh` (display: `"JVC (MakeHex)"`)
    - `panasonic` -> `panasonic_mh` (display: `"Panasonic (MakeHex)"`)
    - `rc5` -> `rc5_mh` (display: `"RC5 (MakeHex)"`)
    - `rc6` -> `rc6_mh` (display: `"RC6 (MakeHex)"`)
    - `sharp` -> `sharp_mh` (display: `"Sharp (MakeHex)"`)
    - `denon` -> `denon_mh` (display: `"Denon (MakeHex)"`)
    - `mitsubishi` -> `mitsubishi_mh` (display: `"Mitsubishi (MakeHex)"`)
  - Preserve original `makehex_name` fields for the underlying MakeHex engine generator.
  - Update `ir_protocols_find()` to resolve both underscore (`_mh`) and hyphen (`-mh`) aliases, ensuring every single protocol in the table is unambiguously accessible.
- **Fix Loop Counter and Input Validation in `irio_pio_tx_frame_write()` (`src/pirate/irio_pio.c`, `src/pirate/irio_pio.h`)**:
  - Fix loop variable type: `uint16_t i` instead of `uint8_t i` to support `pairs >= 256`.
  - Add early guard: return immediately if `buffer == NULL` or `pairs == 0`.
  - Validate modulation carrier frequency: clamp invalid values (`< 10 kHz` or `> 100 kHz`) to 38 kHz to prevent division by zero in PIO clock divider setup.
  - Skip empty zero-duration pairs (`mark == 0 && space == 0`) to prevent PIO state machine stalling or glitching.
  - Add `irio_pio_validate_pairs()` helper function to check timing validity.
- **Timing Validation in Raw AIR Packet Parser (`src/commands/infrared/irtxrx.c`)**:
  - Parse timing values with `uint32_t` before bounds checking.
  - Explicitly reject timing values exceeding 16-bit limits (`> 0xFFFF` / `> 65535`) and reject 0-duration values.
  - Fix string termination bug in command-line AIR packet parsing when appending the terminal semicolon.
- **Command Buffer Hardening (`src/commands/infrared/ir_setprotocol.c`, `src/commands/infrared/ir_send.c`)**:
  - Increase protocol name buffer in `ir_setprotocol.c` to `char name[32]` to safely accommodate the longest protocol names (`dishplayer_network`, `dishplayer_network_mh`).
- **Comprehensive Verification (`tests/test_ir_protocols.c`, `tests/test_ir_protocols.sh`)**:
  - Expand host test suite to verify:
    1. All 7 disambiguated MakeHex protocols resolve cleanly by name (`_mh` and `-mh`).
    2. Zero duplicates exist in the entire 75-entry protocol table.
    3. Timing bounds validation: rejecting `> 0xFFFF` and empty 0-duration pairs.
    4. Loop counter test: verifying transmission with `pairs >= 256` completes without overflow or hang.
    5. Clean execution under AddressSanitizer and UndefinedBehaviorSanitizer.
  - Verify RP2350 target firmware build `bus_pirate6.uf2` within RAM ceiling (<= 380 KB) and fixed 4 KB stacks.

## Capabilities

### Modified Capabilities
- `infrared`: Unambiguous protocol lookup for all native and MakeHex protocols, 16-bit timing validation, 256+ pair loop safety, buffer overflow hardening, and verified `.ir` and TV-B-Gone sequences.

## Impact

- **Memory Footprint**: Static RAM remains well within the 380 KB ceiling (~358 KB used). Zero dynamic allocations in core IR execution paths.
- **Stack Usage**: Fixed 4096-byte stack limits (`SCRATCH_Y` Core 0, `SCRATCH_X` Core 1) strictly preserved.
- **Safety**: Eliminates infinite loop hazard on long IR captures (`pairs >= 256`), eliminates PIO division-by-zero risk on invalid carrier frequencies, and rejects malformed raw timing payloads.
