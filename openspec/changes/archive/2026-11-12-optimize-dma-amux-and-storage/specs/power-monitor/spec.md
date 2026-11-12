# Spec Delta

## MODIFIED Requirements

### Requirement: Single-Core Control Plane ADC Ownership
All periodic analog multiplexer (AMUX) channel sweeping and ADC sampling SHALL be owned and executed by Core 0 (control plane) or offloaded to an autonomous Core 0-supervised DMA sequence. Core 1 SHALL NOT perform unsolicited hardware AMUX sweeping that could load live bus pins or disrupt protocol timing.

#### Scenario: Periodic monitor sweep runs on Core 0
- **GIVEN** Core 0 is executing its periodic cooperative tick
- **WHEN** the system monitor tick interval elapses
- **THEN** Core 0 sweeps the AMUX channels and updates the power monitor state without Core 1 intervention.

#### Scenario: Core 1 avoids direct hardware AMUX sweep
- **GIVEN** a protocol engine or binary mode is active on Core 1
- **WHEN** an ADC status query is requested
- **THEN** Core 1 reads the latest measured values from the published snapshot rather than sweeping hardware AMUX pins directly.

#### Scenario: Autonomous DMA AMUX sampling
- **GIVEN** continuous power monitoring is enabled
- **WHEN** the hardware timer triggers the chained AMUX DMA channel
- **THEN** 16 AMUX channels are selected, sampled, and written to telemetry buffers without CPU polling.
