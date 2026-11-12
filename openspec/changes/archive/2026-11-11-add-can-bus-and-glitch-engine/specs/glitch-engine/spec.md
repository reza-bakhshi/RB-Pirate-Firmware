# Spec Delta

## ADDED Requirements

### Requirement: Synchronized Fault Injection
The glitch engine SHALL generate precision voltage brownout pulses and clock glitch waveforms synchronized to external trigger signals with nanosecond time resolution.

#### Scenario: Voltage glitch generation
- **GIVEN** target hardware powered through the programmable PSU
- **WHEN** the user executes `glitch -v -d <delay_ns> -w <width_ns>`
- **THEN** the override line pulses low for the specified width after the configured delay upon trigger detection.

#### Scenario: External edge trigger synchronization
- **GIVEN** an external trigger pin configured for rising or falling edge
- **WHEN** the trigger event occurs
- **THEN** the fault injection sequence fires with determinism under 20 nanoseconds jitter.
