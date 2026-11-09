# Proposal

## Why
Hardware-in-the-loop (HIL) automation and continuous soak testing are required to ensure stability across hundreds of mode transitions, prevent memory regressions, catch unexpected watchdog reboots, and validate fault injection/recovery on target hardware.

## What Changes
- Implement `tests/hil_test_suite.py` for automated HIL execution over USB serial CDC:
  - Phase 1: Smoke checks (boot banner, info screen, health, fault, memory, features command).
  - Phase 2: Mode cycling (1000 transitions across HiZ, 2WIRE, SPI, I2C, UART, etc.).
  - Phase 3: Binary transport stress and fuzzing (CDC1 framing, buffer overflow resilience).
  - Phase 4: Fault injection and cancellation checks (`Ctrl+C` interrupt, watchdog heartbeat).
  - Phase 5: Long-run soak runner tracking error counter drift and stack watermarks.
- Implement `tests/test_hil_smoke.sh` and `tests/test_soak_runner.sh` for local/CI integration.
- Verify test runners execute cleanly from any directory (ARCH-500).

## Capabilities

### New Capabilities
*(None; `skip_specs: true`)*

### Modified Capabilities
*(None)*

## Impact
- Test infrastructure (`tests/` directory).
- Zero changes to production firmware binaries.
