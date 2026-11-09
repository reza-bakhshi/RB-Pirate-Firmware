# SUBGHZ Specification

## MODIFIED Requirements

### Requirement: Protocol parity

The mode SHALL use the pinned Flipper Zero protocol names, timings and .sub field handling, and SHALL document any deliberate RP2350 divergence.

#### Scenario: Protocol parity
- **WHEN** the behavior related to protocol parity is exercised
- **THEN** A spot-checked protocol transmits and decodes with the expected timing.

#### Scenario: Preset coverage and frequency bounds
- **WHEN** the user selects a preset or frequency
- **THEN** valid CC1101 bands (300-348, 387-464, 779-928 MHz) and presets are accepted and invalid ones rejected.

#### Scenario: Flipper sub file round-trip fidelity
- **WHEN** a Flipper `.sub` file (RAW, BinRAW, or Key) is loaded or saved
- **THEN** fields, custom presets, and pulse timings preserve format parity without corruption.

#### Scenario: Capture interrupt safety
- **WHEN** a capture operation completes or is stopped
- **THEN** edge interrupts on GDO0 are disabled and cleared via `gpio_acknowledge_irq` without stale pending events.

#### Scenario: Transmission safety gates
- **WHEN** transmission is attempted outside standard ISM bands
- **THEN** transmission is blocked unless `txallow` is explicitly enabled.
