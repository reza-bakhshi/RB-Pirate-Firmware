# SUBGHZ Specification

## Purpose

Defines the CC1101 SubGHz mode: frequency and preset setup, scanning, receiving, recording, replay, protocol decoding, key handling, transmission and jamming.

## Requirements

### Requirement: Mode activation and teardown

The SUBGHZ capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

### Requirement: Command coverage

The SUBGHZ capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: setfrequency command
- **WHEN** the user runs `setfrequency`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: rssi command
- **WHEN** the user runs `rssi`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: preset command
- **WHEN** the user runs `preset`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: protocol command
- **WHEN** the user runs `protocol`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: info command
- **WHEN** the user runs `info`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: scan command
- **WHEN** the user runs `scan`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: sweep command
- **WHEN** the user runs `sweep`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: trace command
- **WHEN** the user runs `trace`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: waterfall command
- **WHEN** the user runs `waterfall`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: receive command
- **WHEN** the user runs `receive`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: hop command
- **WHEN** the user runs `hop`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: replay command
- **WHEN** the user runs `replay`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: record command
- **WHEN** the user runs `record`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: load command
- **WHEN** the user runs `load`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: send command
- **WHEN** the user runs `send`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: decode command
- **WHEN** the user runs `decode`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: clone command
- **WHEN** the user runs `clone`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: savekey command
- **WHEN** the user runs `savekey`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: monitor command
- **WHEN** the user runs `monitor`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: keyload command
- **WHEN** the user runs `keyload`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: keys command
- **WHEN** the user runs `keys`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: keyadd command
- **WHEN** the user runs `keyadd`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: txallow command
- **WHEN** the user runs `txallow`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: power command
- **WHEN** the user runs `power`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: bitrate command
- **WHEN** the user runs `bitrate`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: scanprofile command
- **WHEN** the user runs `scanprofile`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: jam command
- **WHEN** the user runs `jam`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: bruteforce command
- **WHEN** the user runs `bruteforce`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: burst command
- **WHEN** the user runs `burst`
- **THEN** the command completes with a meaningful result or a clear error

#### Scenario: ear command
- **WHEN** the user runs `ear`
- **THEN** the command completes with a meaningful result or a clear error

### Requirement: Failure handling

The SUBGHZ capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

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
