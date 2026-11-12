# Spec Delta

## ADDED Requirements

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
