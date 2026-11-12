# hardware-sha256 Specification

## Purpose
Provides hardware-accelerated SHA-256 cryptographic digest calculation for files on storage media and in-memory buffers utilizing the RP2350 SHA-256 accelerator FIFO and streaming engines with zero runtime heap allocation.

## Requirements

### Requirement: Hardware-Accelerated SHA-256 Calculation
The system SHALL provide hardware-accelerated SHA-256 calculation through a dedicated CLI command and library interface utilizing RP2350 hardware peripherals without runtime heap allocation.

#### Scenario: File hashing from storage
- **GIVEN** a file stored on the TF card
- **WHEN** the user executes `sha256 <filename>`
- **THEN** the system streams the file in bounded static blocks, calculates the 256-bit digest using the RP2350 SHA-256 engine, and prints the formatted hexadecimal hash.

#### Scenario: Memory buffer hashing
- **GIVEN** a valid memory address and byte length
- **WHEN** an internal service requests a SHA-256 digest of a memory block
- **THEN** the accelerator calculates the digest at full bus speed and returns the result.

#### Scenario: Missing or unreadable file error handling
- **GIVEN** a non-existent or corrupted file on the TF card
- **WHEN** the user executes `sha256 <invalid_filename>`
- **THEN** the command aborts cleanly with a descriptive error message without hanging.
