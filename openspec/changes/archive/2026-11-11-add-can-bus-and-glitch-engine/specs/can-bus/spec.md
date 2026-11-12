# Spec Delta

## ADDED Requirements

### Requirement: CAN Bus Mode Activation and Operations
The CAN mode SHALL configure PIO state machines and buffered IO pins to interface with external CAN transceivers, supporting frame transmission, passive bus sniffing, and bitrates up to 1 Mbps.

#### Scenario: Successful CAN mode initialization
- **WHEN** the user selects CAN mode and configures bitrate
- **THEN** the mode claims required PIO resources under `BP_RES_OWNER_MODE_BASE` and reports ready.

#### Scenario: Passive frame sniffing
- **GIVEN** active CAN bus traffic
- **WHEN** the user runs `sniff`
- **THEN** standard and extended CAN frames are decoded and printed with timestamp, arbitration ID, DLC, and payload bytes.

#### Scenario: Frame transmission
- **GIVEN** an active CAN bus
- **WHEN** the user transmits a CAN frame with arbitration ID and data bytes
- **THEN** the frame is clocked out with proper CRC, ACK slot, and inter-frame spacing.

#### Scenario: Clean mode exit
- **WHEN** the user leaves CAN mode
- **THEN** all PIO programs and state machines are unregistered and released without leaks.
