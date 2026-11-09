# Design

## Context
See `proposal.md`. Host unit tests run under mock environments, but true embedded reliability demands testing actual RP2350 hardware over USB serial (CDC0 terminal and CDC1 binary mode). Automating HIL test phases allows regression detection during soak runs without requiring manual terminal interaction.

## Goals / Non-Goals

**Goals:**
- Provide a standalone Python/shell test suite that connects to an RB-Pirate over USB CDC.
- Execute smoke, mode transitions (1000+ cycles), binary protocol stress, and soak cycles without resets or unhandled exceptions.
- Provide a simulated stub mode when hardware is not physically attached, ensuring CI runners can execute the test harness deterministically.

**Non-Goals:**
- Replacing existing host unit tests.

## Decisions

### Decision: Python + PySerial harness with fallback mock mode
- *Rationale*: Python provides clean cross-platform serial communication, regex assertion matching, timing measurement, and json/log artifact emission. Providing a mock mode allows full CI execution even on headless builders without attached physical hardware.

## Risks / Trade-offs
- *Risk*: Serial port name changes between OS environments (`/dev/ttyACM0` vs `COM3`).
  - *Mitigation*: Auto-detection via USB VID/PID (0x2e8a / 0x000a or Bus Pirate descriptors) with `HIL_PORT` environment variable override.
