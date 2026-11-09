# core-separation Specification

## Purpose
Defines the architectural separation between the Control Plane (Core 0: user interface, shell, command parsing, storage, and supervisory monitoring) and the Hardware Data Plane (Core 1: protocol execution, PIO, DMA, captures, and fast IRQs), along with mode lifecycle management, protocol heartbeats, and fault recovery.

## Requirements

### Requirement: Control and Protocol Core Separation
The system SHALL dedicate Core 0 to control plane operations (command line interface, syntax compilation, storage filesystem, and watchdog supervision) and Core 1 to the protocol hardware data plane (protocol engines, PIO/DMA execution, hardware captures, and fast IRQs).

#### Scenario: Responsive control plane during protocol operations
- **GIVEN** a protocol engine or hardware capture executing on Core 1
- **WHEN** user input, terminal status requests, or watchdog supervision occurs on Core 0
- **THEN** Core 0 remains interactive and processes events without blocking on Core 1 execution

#### Scenario: No cross-core direct function calls
- **GIVEN** any interaction between Core 0 and Core 1
- **WHEN** commands, progress updates, or status changes are communicated
- **THEN** all communication occurs exclusively through typed SPSC queues, message events, or seqlock snapshots

### Requirement: Mode Lifecycle Descriptor and Transactional Switch
The system SHALL manage all protocol modes through a unified `bp_mode_t` descriptor interface and SHALL execute mode switches transactionally via `bp_mode_switch()`.

#### Scenario: Clean mode transition
- **GIVEN** an active protocol mode
- **WHEN** switching to a new protocol mode via `bp_mode_switch()`
- **THEN** the active mode is halted, all its peripheral resource leases are released, BIO pins are returned to safe floating HiZ state, and the new mode is configured and started

#### Scenario: Global cleanup before setup ordering
- **GIVEN** any mode change command or binary mode invocation
- **WHEN** a mode switch is initiated
- **THEN** the active mode's cleanup function is executed before the target mode's setup function is invoked

### Requirement: Automatic Rollback to HiZ on Setup Failure
The system SHALL automatically rollback to the safe HiZ mode and release all partial resource leases whenever a target mode's setup or hardware activation fails.

#### Scenario: Target mode setup failure
- **GIVEN** a mode switch attempt to a mode whose hardware activation fails
- **WHEN** `start()` returns a hardware error
- **THEN** partial leases are reclaimed, safe HiZ mode is activated, all pins remain floating, and an informative error is returned to the user without panicking

### Requirement: Protocol Heartbeat and Fault Recovery
The system SHALL monitor the execution health of the Core 1 protocol data plane using dedicated heartbeats and SHALL recover safely to HiZ upon protocol stalls or faults.

#### Scenario: Protocol engine stall recovery
- **GIVEN** a protocol execution on Core 1 that stalls or experiences an unrecoverable fault
- **WHEN** the supervisory watchdog detects a protocol heartbeat timeout
- **THEN** the stalled protocol is marked as faulted, its hardware leases and pins are reset to safe states, and the system recovers to the HiZ prompt
