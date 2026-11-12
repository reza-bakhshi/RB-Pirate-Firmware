# Design: Subsystem Hardening & Clock Scaling

## Core Ownership & Concurrency
- `ps2.c`: Executed as an asynchronous job under `BP_RES_OWNER_JOB` on Core 1. PIO state machine allocation is performed using `bp_res_claim_unused_pio_sm()`. Redundant calls to `pio_sm_claim()` are eliminated. Pin direction setup on PIO1 uses `kbd_h2d_sm`, while PIO0 uses `kbd_sm`.
- `irtxrx.c`: Runs on Core 0 parser / Core 1 transmitter. String scanning validates string length (`strnlen`) and delimiter presence before advancing pointer offsets.
- `pwm.c`: Calculates timer clock division factor using `clock_get_hz(clk_sys)`.

## Memory & Leases
- Zero dynamic allocation. All buffers remain static and bounded.
- Leases: 4 PIO state machines for PS/2, 1 PWM slice for PWM, 1 PIO state machine for IR TX.

## Rollback & Safety
- On parse failure in `irtx_transmit()`, the function returns `false` without modifying hardware registers.
- On PS/2 initialization failure, `bp_res_release_all(BP_RES_OWNER_JOB)` unclaims all acquired SMs.
