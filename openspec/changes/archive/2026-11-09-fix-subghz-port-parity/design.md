# Design: SubGHz CC1101 Port Parity and Protocol Conformance (F4)

## Architecture Overview

The RB-Pirate SubGHz subsystem provides RF capture, analysis, decoding, and transmission using a Texas Instruments CC1101 transceiver controlled via SPI, paired with an asynchronous pulse capture engine on RP2350/RP2040 GPIOs.

```
                      +-----------------------------+
                      |       Core 0 Control        |
                      |  CLI / UI / .sub File I/O   |
                      +--------------+--------------+
                                     |
                                     v
                       bp_job_dispatch(Core 1)
                                     |
            +------------------------+------------------------+
            |                                                 |
            v                                                 v
  +--------------------+                             +--------------------+
  | CC1101 Driver      |                             | SubGHz Cap Engine  |
  | SPI commands       |                             | GDO0 Edge Capture  |
  | Registers, Presets |                             | Ring Buffer        |
  | PATABLE, RSSI      |                             | IRQ Acknowledge    |
  +--------------------+                             +--------------------+
```

## Detailed Design Aspects

### 1. Frequency Calculation & Hardware Bands
The CC1101 features an internal frequency synthesizer clocked by a 26.0 MHz quartz crystal.
Frequency word calculation formula:
$$FREQ = \left\lfloor \frac{f_{\text{carrier}} \times 2^{16} + 13000}{26000} \right\rfloor$$
Hardware operating bands:
- 300.0 MHz – 348.0 MHz
- 387.0 MHz – 464.0 MHz
- 779.0 MHz – 928.0 MHz

A unified helper `cc1101_valid_frequency(uint32_t khz)` enforces valid hardware bounds across `cc1101.c`, `subghz.c`, and `subghz_file.c`.

### 2. Preset Definitions & Flipper Aliases
Canonical presets include:
- `Ook270Async`: 270 kHz RX bandwidth, asynchronous serial data on GDO0.
- `Ook650Async`: 650 kHz RX bandwidth, asynchronous serial data on GDO0.
- `2FSKDev238Async`: 2-FSK, 2.38 kHz deviation, 270.8 kHz RX BW.
- `2FSKDev12KAsync`: 2-FSK, 12 kHz deviation, 270.8 kHz RX BW.
- `2FSKDev476Async`: 2-FSK, 47.6 kHz deviation, 270.8 kHz RX BW.
- `GFSK10K`: GFSK, 9.99 kbps, 19.0 kHz deviation, sync-word capable.
- `MSK100K`: MSK, 99.97 kbps, 100 kHz deviation.
- `Custom`: user/file-defined register pairs and optional 8-byte PATABLE.

Name resolution (`cc1101_preset_from_name`) supports standard names, Flipper OS canonical strings (`FuriHalSubGhzPreset...`), and profile aliases (`AM270`, `AM650`, `FM238`, `FM12K`, `FM476`, `Sniff`, `Scan`, `RawSend`).

### 3. Edge Capture Interrupt Safety
When receiving OOK or FSK signals via GDO0:
- `subghz_cap_start(bio_pin)` maps the BIO pin to RP2350 GPIO, configures it as input, enables edge rise/fall interrupts, and attaches `subghz_cap_handler`.
- In `subghz_cap_stop()`:
  - Interrupts are disabled atomically (`save_and_disable_interrupts()`).
  - Active flag `cap_active` is set to `false`.
  - The GPIO edge interrupt is disabled (`gpio_set_irq_enabled(gpio, ..., false)`).
  - Crucially, `gpio_acknowledge_irq(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)` is called to clear any pending interrupt condition before removing the raw handler.
  - The handler is unmasked and `cap_gpio` is set to `0xff`.

### 4. Flipper `.sub` File Format Fidelity
Round-trip fidelity is ensured across:
- **RAW files**: Alternating polarity pulse stream (`RAW_Data: 350 -700 700 ...`).
- **BinRAW files**: `Bit: <total>`, `TE: <us>`, repeated `Bit_RAW: <n>`, `Data_RAW: <hex bytes>`.
- **Key files**: Protocol-specific decoding with `Protocol`, `Bit`, `Key`, and optional `Manufacture` or `Duration_Counter`.
- **Custom Presets**: `Custom_preset_module: CC1101` and `Custom_preset_data:` with hex register pairs and `00 00` terminated 8-byte PATABLE.

### 5. Transmission Safety Gates
- Strict mode allows transmission only within recognized ISM bands:
  - 314.5 – 315.5 MHz
  - 433.05 – 434.79 MHz
  - 868.0 – 868.6 MHz
  - 902.0 – 928.0 MHz
- `txallow open` allows testing across the full tuning range with explicit confirmation.
- Any long-running transmission or jamming job can be interrupted via Ctrl+C / `bp_job_check_cancelled()` in $< 100\text{ ms}$, immediately returning the CC1101 to `IDLE` and releasing PWM/GPIO resources.
