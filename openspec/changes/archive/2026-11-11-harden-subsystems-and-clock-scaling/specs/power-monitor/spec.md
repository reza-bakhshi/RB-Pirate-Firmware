# Spec Delta

## MODIFIED Requirements

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
