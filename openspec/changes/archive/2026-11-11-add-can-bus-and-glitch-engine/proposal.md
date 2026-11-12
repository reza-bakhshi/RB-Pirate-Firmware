# Proposal

## Why
Automotive systems, industrial PLCs, and secure microcontrollers rely heavily on Controller Area Network (CAN) buses and require fault-injection testing (voltage glitching and clock glitching) for hardware security assessments. RB-Pirate features fast programmable power supplies, a discrete override transistor, and 12 PIO state machines on RP2350B, but currently lacks an integrated CAN bus protocol mode and a synchronized fault-injection command engine. Adding these capabilities expands RB-Pirate into an advanced automotive and hardware security analysis toolkit.

## What Changes
- **CAN Bus Protocol Mode (`src/mode/can.c`)**:
  - Implement a full-featured CAN mode using a PIO-driven CAN engine (`can2040` port).
  - Support standard 11-bit and extended 29-bit identifiers at bitrates from 20 kbps to 1 Mbps.
  - Support passive bus sniffing, ID/mask hardware filtering, error counter telemetry, and frame transmission.
- **Hardware Fault Injection Engine (`src/commands/global/cmd_glitch.c`)**:
  - Implement a synchronized `glitch` command engine utilizing PIO and PWM timers.
  - Support voltage brownout / crowbar glitching via the programmable PSU override transistor with nanosecond pulse widths.
  - Support clock glitching (burst and glitch insertion) triggered on external GPIO edge transitions, UART/SPI trigger patterns, or cycle counts.

## Capabilities

### New Capabilities
- `can-bus`: Provides PIO-based CAN bus frame sniffing, transmission, filtering, and error monitoring up to 1 Mbps.
- `glitch-engine`: Provides nanosecond-resolution voltage and clock fault injection synchronized to external trigger events.

### Modified Capabilities
*(None)*

## Impact
- Core 1 (Protocol plane): `src/mode/can.c`, `src/mode/can.h`, `src/pirate/hwcan_pio.c`, `src/commands/global/cmd_glitch.c`.
- Requires 1 PIO program and 2 state machines for CAN TX/RX, and 1 state machine or PWM slice for glitch timing.
- Zero heap allocation: Fixed circular ring buffers for CAN frame queues.
