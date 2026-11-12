# Spec Delta

## ADDED Requirements

### Requirement: Scaled Logic Capture Buffer Depth
The binary modes logic capture subsystem SHALL support sample buffer depths up to 256 KB on RP2350 platforms while preserving bounded SRAM headroom.

#### Scenario: 256 KB sample buffer allocation on RP2350
- **GIVEN** an RP2350B platform with 520 KB SRAM
- **WHEN** the logic analyzer or SUMP mode initializes capture storage
- **THEN** up to 256 KB of contiguous static capture buffer is available without exceeding total static memory bounds.
