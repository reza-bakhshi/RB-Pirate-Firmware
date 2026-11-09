# Design: Fix Infrared Port Parity and Protocol Ambiguities

## 1. Architecture Overview

The INFRARED subsystem on RB-Pirate RP2350 comprises:
1. **Low-Level PIO Drivers (`src/pirate/irio_pio.c`, `src/pirate/irio.pio`)**:
   - `ir_out` PIO state machine: timed carrier gating using 16-bit mark and space counts.
   - `ir_out_carrier` PIO state machine: high-frequency PWM carrier generator (e.g. 36 kHz - 56 kHz).
   - `ir_in_low_counter` and RX state machines: mark/space edge interval measurement.
2. **Table-Driven Protocol Engine (`src/pirate/ir_protocols.c`, `src/pirate/ir_protocols.h`)**:
   - 13 hand-written native encoders and decoders (NEC, Samsung, LG, Sony, JVC, Panasonic, RC5, RC6, Dish, Sharp, Denon, Mitsubishi, SharpK).
   - 59 MakeHex/IRP definitions (`src/lib/makehex/MakeHex.cpp`, `irp_definitions.c`) generated dynamically from standard IRP formulas.
3. **INFRARED User Commands (`src/commands/infrared/`)**:
   - `send`, `receive`, `setprotocol`, `record`, `load`, `replay`, `jam`, `devicebgone`, `tvbgone`, `config`, `irtx`, `irrx`.

```
+-----------------------------------------------------------------------------------+
| INFRARED Subsystem Architecture                                                   |
|                                                                                   |
| [CLI Commands: send, setprotocol, load, replay, etc.]                            |
|                        |                                                          |
|                        v                                                          |
|           [ir_protocols_find(name)]                                               |
|           ├── Match native arg_name (e.g. "jvc", "rc5")                           |
|           └── Match MakeHex alias (e.g. "jvc_mh", "jvc-mh")                       |
|                        |                                                          |
|                        v                                                          |
|           [Protocol Encoder / Generator]                                          |
|           ├── Hand-written encoder: proto->encode(dev, sub, cmd, pairs, &count)   |
|           └── MakeHex IRP generator: makehex_generate(proto->makehex_name, ...)   |
|                        |                                                          |
|                        v                                                          |
|           [Timing Bounds & Pair Sanitization]                                     |
|           ├── 16-bit bounds validation: 1 <= us <= 0xFFFF                         |
|           └── Reject empty/zero pairs: !(mark == 0 && space == 0)                 |
|                        |                                                          |
|                        v                                                          |
|           [irio_pio_tx_frame_write(carrier_hz, pairs, buffer)]                    |
|           ├── 16-bit loop counter: for (uint16_t i = 0; i < pairs; i++)           |
|           ├── Carrier frequency safety clamp (10 kHz - 100 kHz)                   |
|           └── Push to PIO TX FIFO & wait for idle                                 |
+-----------------------------------------------------------------------------------+
```

## 2. Component Design

### 2.1 Protocol Disambiguation and Aliasing (`src/pirate/ir_protocols.c`)

In `ir_protocol_table`, seven MakeHex entries collide with native protocols. We disambiguate the MakeHex entries by giving them explicit `_mh` argument names and descriptive display strings while preserving the exact `makehex_name` for generator lookup:

| Protocol | Native `arg_name` | Native Display | MakeHex `arg_name` | MakeHex Display | `makehex_name` |
|---|---|---|---|---|---|
| JVC | `jvc` | `JVC` | `jvc_mh` | `JVC (MakeHex)` | `jvc` |
| Panasonic | `panasonic` | `PANASONIC` | `panasonic_mh` | `Panasonic (MakeHex)` | `panasonic` |
| RC5 | `rc5` | `RC5` | `rc5_mh` | `RC5 (MakeHex)` | `rc5` |
| RC6 | `rc6` | `RC6` | `rc6_mh` | `RC6 (MakeHex)` | `rc6` |
| Sharp | `sharp` | `SHARP` | `sharp_mh` | `Sharp (MakeHex)` | `sharp` |
| Denon | `denon` | `DENON` | `denon_mh` | `Denon (MakeHex)` | `denon` |
| Mitsubishi | `mitsubishi` | `MITSUBISHI` | `mitsubishi_mh` | `Mitsubishi (MakeHex)` | `Mitsubishi` |

Lookup function `ir_protocols_find(const char *arg_name)` is updated to:
1. First perform direct case-insensitive comparison against `ir_protocol_table[i].arg_name`.
2. Second, perform normalized comparison replacing `-` with `_` (e.g. `jvc-mh` matches `jvc_mh`).

This ensures:
- Native protocols remain selectable via standard short names (`jvc`, `panasonic`, `rc5`, `rc6`, `sharp`, `denon`, `mitsubishi`).
- MakeHex variants are explicitly selectable via `_mh` or `-mh`.
- Every entry in `ir_protocol_table` (all 75 entries) has a unique name.

### 2.2 PIO Frame Transmission Hardening (`src/pirate/irio_pio.c`, `irio_pio.h`)

1. **16-bit Loop Counter**:
   In `irio_pio_tx_frame_write()`:
   ```c
   void irio_pio_tx_frame_write(float mod_freq, uint16_t pairs, uint32_t *buffer) {
       if (buffer == NULL || pairs == 0) {
           return;
       }
       if (mod_freq < 10000.0f || mod_freq > 100000.0f) {
           mod_freq = 38000.0f;
       }
       irio_pio_tx_set_mod_freq(mod_freq);

       for (uint16_t i = 0; i < pairs; i++) {
           uint32_t val = buffer[i];
           if (val == 0) {
               continue; // drop zero dummy words
           }
           pio_sm_put_blocking(pio_config_tx.pio, pio_config_tx.sm, val);
       }
       if (!pio_sm_wait_idle(pio_config_tx.pio, pio_config_tx.sm, 0xfffff)) {
           printf("PIO TX timeout\r\n");
       }
   }
   ```
2. **Timing Validation Helper**:
   `bool irio_pio_validate_pairs(uint16_t pairs, const uint32_t *buffer)` validates that all words contain valid non-zero mark/space values within 16-bit limits.

### 2.3 AIR Packet and Raw Timing Bounds (`src/commands/infrared/irtxrx.c`)

In `irtx_transmit()`:
- Decimal parsing uses `uint32_t value`.
- If `value == 0` or `value > 0xffff`, parsing immediately aborts with a diagnostic error, rejecting malformed packets before PIO setup.
- Semicolon appending in CLI argument handling uses explicit length checking and proper null termination:
  ```c
  size_t len = strlen(buffer);
  if (len + 1 < sizeof(buffer)) {
      buffer[len] = ';';
      buffer[len + 1] = '\0';
  }
  ```

### 2.4 Command Buffer Sizing (`src/commands/infrared/ir_setprotocol.c`)

- Increase `char name[17]` to `char name[32]` in `ir_setprotocol_handler()`. The longest protocol name `dishplayer_network` is 18 characters; `name[32]` prevents any potential truncation.

## 3. Verification Strategy

1. **Host-Side Unit Tests (`tests/test_ir_protocols.c`, `tests/test_ir_protocols.sh`)**:
   - Verify table uniqueness: assert 0 duplicates across all 75 entries.
   - Verify lookup of both native and `_mh` / `-mh` variants for all 7 disambiguated protocols.
   - Verify 16-bit timing limits and empty pair rejection in `irio_pio_validate_pairs()`.
   - Verify pair count >= 256 loop test.
   - Run under AddressSanitizer and UndefinedBehaviorSanitizer.
2. **Full Host Test Suite**:
   - Run all test scripts in `tests/*.sh` to prevent regressions.
3. **RP2350 Target Build**:
   - Compile `bus_pirate6.uf2` using `make -C build_rp2350 -j$(nproc)`.
   - Check RAM ceiling <= 380 KB and fixed 4 KB stacks (`SCRATCH_Y`, `SCRATCH_X`).
