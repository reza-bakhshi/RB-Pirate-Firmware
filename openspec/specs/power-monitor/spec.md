# power-monitor Specification

## Purpose
Defines control plane ownership of analog multiplexer (AMUX) sampling, ADC peripheral arbitration, lockless power/voltage monitor snapshot publication, and PSU fault supervision.

## Requirements

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

### Requirement: Reentrant and Owner-Aware ADC Arbitration
The ADC arbitration helper (`adc_busy_wait`) SHALL track the owning core ID and nesting depth. If the current core already owns the ADC lock, nested acquisition SHALL succeed immediately without deadlocking. Lock acquisition by another core SHALL use bounded waits with backoff to prevent bus starvation.

#### Scenario: Reentrant lock acquisition on the same core
- **GIVEN** Core 0 has acquired the ADC lock for a measurement sequence
- **WHEN** a nested function on Core 0 calls `adc_busy_wait(true)`
- **THEN** the call succeeds immediately, incrementing the nesting depth.

#### Scenario: Lock release unwinds nesting correctly
- **GIVEN** Core 0 holds the ADC lock with a nesting depth greater than 1
- **WHEN** `adc_busy_wait(false)` is invoked
- **THEN** the nesting depth is decremented, and the hardware spinlock is only released when nesting reaches zero.

### Requirement: Seqlock-Protected Power and Monitor Snapshot Publication
The system SHALL publish measured pin voltages, Vreg, Vout, current sense, fuse status, and PSU fault flags into the atomic snapshot structure (`bp_ui_snapshot_t`). The snapshot SHALL be updated with memory barriers and protected by a monotonic sequence counter (seqlock). Consumers SHALL read consistent values without blocking or taking mutexes.

#### Scenario: Atomic snapshot read during power update
- **GIVEN** Core 0 is updating power monitor readings in the snapshot
- **WHEN** a UI toolbar or Core 1 task invokes `bp_ui_snapshot_read()`
- **THEN** the reader detects the sequence counter state and returns a consistent, uncorrupted snapshot.

### Requirement: Authoritative Control-Plane PSU Fault Trip and Job Abort
Core 0 SHALL periodically poll the power supply unit (PSU) current fuse and undervoltage status. If an overcurrent or undervoltage condition is detected, Core 0 SHALL immediately disable PSU output, flag the fault in runtime state and snapshot, and signal cancellation of any running protocol job on Core 1 to protect attached hardware. Transient inrush when charging capacitive loads SHALL be recovered gracefully.

#### Scenario: Overcurrent condition triggers immediate PSU shutdown
- **GIVEN** the PSU is enabled and supplying power to external target hardware
- **WHEN** measured current exceeds the hardware trip threshold
- **THEN** Core 0 immediately turns off the PSU regulator, updates the fault flags, and aborts any active Core 1 protocol job.

#### Scenario: Capacitive inrush transient recovery
- **GIVEN** the PSU is requested to enable into a capacitive load
- **WHEN** inrush current occurs during rail capacitor charging
- **THEN** the subsystem engages temporary override, clears the transient trip after settling, and enters steady-state regulation without permanent error.

### Requirement: Power Profiling and Coulomb Counting
The power monitor subsystem SHALL support continuous current and power integration over time, computing accumulated charge in milliampere-hours ($\text{mAh}$) and total energy in milliwatt-hours ($\text{mWh}$).

#### Scenario: Continuous energy integration
- **GIVEN** the PSU is supplying power to a target device
- **WHEN** the user runs `w.profile`
- **THEN** instantaneous voltage, current, power, accumulated charge ($\text{mAh}$), and total energy ($\text{mWh}$) update continuously on the console.

#### Scenario: Real-time CSV power logging
- **GIVEN** `w.profile -f <filename>` is invoked
- **WHEN** periodic telemetry updates occur
- **THEN** timestamped measurement rows are flushed to the designated CSV file on storage.
