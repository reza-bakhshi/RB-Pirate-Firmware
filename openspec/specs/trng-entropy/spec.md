# trng-entropy Specification

## Purpose
Provides high-quality hardware entropy and true random number generation (TRNG) utilizing RP2350 hardware ring oscillators and entropy registers for cryptographic seeding, noise generation, and protocol test patterns with zero runtime heap allocation.

## Requirements

### Requirement: Hardware True Random Number Generation
The system SHALL provide a non-blocking hardware entropy source utilizing RP2350 hardware registers to generate 32-bit random values.

#### Scenario: Entropy generation command
- **WHEN** the user executes `rand` or `entropy`
- **THEN** 32-bit hardware-generated random words are printed to the terminal.

#### Scenario: Subsystem entropy injection
- **GIVEN** an active SubGHz or Infrared noise jamming mode
- **WHEN** random carrier or timing intervals are generated
- **THEN** the values are sourced from the hardware TRNG instead of a deterministic pseudo-random sequence.
