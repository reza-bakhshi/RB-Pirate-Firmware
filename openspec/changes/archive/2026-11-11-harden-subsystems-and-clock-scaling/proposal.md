# Proposal

## Why
Latent state machine indexing bugs and unchecked array indexing in peripheral drivers can cause system panics and buffer overruns during live protocol exploration. Specifically, `src/mode/ps2.c` applies pin direction configurations to the wrong PIO state machine (`kbd_sm` on `pio1` instead of `kbd_h2d_sm`) and executes redundant `pio_sm_claim` calls that trigger runtime assertion panics under debug builds. Furthermore, `src/commands/infrared/irtxrx.c` parses untrusted CSV packet strings with unbounded index loops, creating a stack buffer overflow vulnerability, and `src/commands/global/pwm.c` calculates pulse widths assuming a legacy 125 MHz clock rather than RP2350's 150 MHz clock. Addressing these issues hardens the subsystem foundations and ensures frequency accuracy across overclocked and RP2350 configurations.

## What Changes
- **PS/2 Sniffer Hardening (`src/mode/ps2.c`)**:
  - Replace incorrect `pio_sm_set_consecutive_pindirs(pio1, kbd_sm, ...)` with `kbd_h2d_sm`.
  - Remove redundant `pio_sm_claim(pio1, sm_h2d)` calls that conflict with the leases already claimed by `bp_res_claim_unused_pio_sm()`.
- **Infrared AIR Packet Bounds Hardening (`src/commands/infrared/irtxrx.c`)**:
  - Add explicit bounds checking against buffer length during delimiter scanning (`$`, `:`, `,`, `;`) in `irtx_transmit()`.
  - Enforce `datacnt < count_of(data)` pre-conditions on both `mark` and `space` phases to eliminate buffer overruns.
- **Clock-Independent PWM Frequency Scaling (`src/commands/global/pwm.c`)**:
  - Replace the hardcoded 125 MHz (16 ns) assumption with `clock_get_hz(clk_sys)` dynamic scaling.
  - Ensure frequency and duty cycle calculations remain exact across default 150 MHz RP2350 and overclocked core frequencies.
- **PSU Transient Inrush Auto-Recovery (`src/pirate/psu.c`)**:
  - Implement a configurable hiccup/retry policy (up to 3 retries with 10 ms backoff) in `psu_enable()` when charging high-capacitance loads without tripping the persistent overcurrent lock.

## Capabilities

### New Capabilities
*(None)*

### Modified Capabilities
- `ps2-sniffer`: Enforce lease-compliant PIO state machine configuration and direction assignment.
- `infrared`: Enforce bounded packet parsing and strict array bounds validation in AIR transmission.
- `power-monitor`: Add transient inrush retry policy and current fuse trip recovery.

## Impact
- Core 0 (Control plane) and Core 1 (Protocol plane): `src/mode/ps2.c`, `src/commands/infrared/irtxrx.c`, `src/commands/global/pwm.c`, `src/pirate/psu.c`.
- Eliminates debug assert panics in PS/2 sniffer and stack overflow vulnerabilities in Infrared AIR transmission.
- Fixes PWM output timing accuracy on RP2350.
