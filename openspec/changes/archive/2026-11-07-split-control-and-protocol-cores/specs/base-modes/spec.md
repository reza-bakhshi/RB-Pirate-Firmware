# Spec Delta: base-modes

## Purpose

Extends the base protocol modes specification to mandate transactional mode transitions via `bp_mode_switch()`, cleanup-before-setup sequencing, and automatic rollback to HiZ with lease reclamation upon setup failure.

## MODIFIED Requirements

### Requirement: Mode activation and teardown
The Base Protocol Modes capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit. All mode transitions SHALL execute the active mode cleanup before configuring the target mode.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

#### Scenario: Transactional mode switch with cleanup before setup
- **GIVEN** an active base mode
- **WHEN** a mode switch to another base mode is initiated
- **THEN** the active mode is cleaned up and its resource leases released prior to the target mode's setup execution

## ADDED Requirements

### Requirement: Transactional Rollback to HiZ
The mode manager SHALL guarantee transactional safety during mode switches by automatically restoring the HiZ state and reclaiming partial resource leases if the target mode fails to initialize.

#### Scenario: Failed mode activation recovery
- **GIVEN** an active protocol mode
- **WHEN** switching to a mode that fails during hardware setup
- **THEN** the partial mode's resources are released, the system cleanly reverts to HiZ mode, and pins remain floating and safe
