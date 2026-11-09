# resource-ownership Specification

## Purpose
The resource-ownership capability establishes strict lease tracking, conflict rejection, and transactional teardown for hardware resources (PIO state machines, DMA channels, IRQ handlers, hardware communication peripherals, and big-buffer memory) to eliminate peripheral collisions and resource leaks without dynamic memory allocation.

## Requirements

### Requirement: Explicit Hardware Resource Leases
The system SHALL require all hardware resources (PIO state machines, DMA channels, hardware IRQ vectors, and the big buffer) to be acquired through explicit, named leases returning status codes instead of invoking panics or hardware asserts.

#### Scenario: Successful lease acquisition
- **GIVEN** a free hardware resource
- **WHEN** an owner requests a lease with valid resource class and identifier
- **THEN** the lease manager marks the resource acquired by the owner and returns `BP_STATUS_OK`

#### Scenario: Contested resource rejection
- **GIVEN** a resource currently leased to owner A
- **WHEN** owner B attempts to acquire or claim the same resource
- **THEN** the lease manager denies the request, returns `BP_STATUS_ERR_RESOURCE`, and leaves owner A's lease untouched

#### Scenario: Invalid resource index rejection
- **GIVEN** an out-of-bounds resource identifier
- **WHEN** a module attempts to acquire or release the resource
- **THEN** the lease manager returns `BP_STATUS_ERR_INVALID` without asserting

### Requirement: Transactional Teardown and Mode Cleanup
The system SHALL provide transactional release (`bp_res_release_all`) that revokes and resets every hardware resource leased to an owner upon mode exit or initialization failure, ensuring zero leaked hardware blocks across mode transitions.

#### Scenario: Clean mode exit unclaims all leases
- **GIVEN** an active mode holding multiple PIO, DMA, and IRQ leases
- **WHEN** the mode exits or unloads
- **THEN** all leases associated with that owner are released and returned to the unallocated pool

#### Scenario: Mode initialization failure rolls back partial leases
- **GIVEN** a mode fails during setup after acquiring partial resources
- **WHEN** error recovery invokes `bp_res_release_all` for that mode
- **THEN** all partially acquired resources are released, preventing resource leakage

#### Scenario: Repeated mode switches leave zero residual claims
- **GIVEN** repeated transitions between protocol modes
- **WHEN** 1000 simulated mode transitions occur
- **THEN** the resource table remains completely free upon return to idle/HiZ mode

### Requirement: Thread-Safe Named Big-Buffer Leasing
The system SHALL manage the 128 KB big buffer as a named resource lease without invoking `printf` from critical sections and returning NULL or error status safely when contested.

#### Scenario: Contested big buffer allocation fails gracefully
- **GIVEN** the big buffer is currently leased by owner A
- **WHEN** owner B attempts to allocate the big buffer
- **THEN** the allocator returns NULL immediately without printing to stdout or causing a hardware fault

#### Scenario: Release returns big buffer to unowned pool
- **GIVEN** the big buffer is leased to an owner
- **WHEN** the owner releases the buffer
- **THEN** the buffer becomes available for subsequent leasing and the owner status resets to unowned

### Requirement: Resource Inspection and Diagnostic Reporting
The system SHALL provide a CLI command `res` and an API `bp_res_print_table()` to display all hardware resources, their active owners, and allocation status.

#### Scenario: Shell prints resource table
- **WHEN** the user executes the `res` command in the shell
- **THEN** the system prints a formatted table displaying all PIO blocks, state machines, DMA channels, IRQs, and big buffer status with their respective owners

#### Scenario: Idle state displays all resources free
- **GIVEN** the system is in HiZ mode with no active background jobs or captures
- **WHEN** the `res` command is executed
- **THEN** all transient protocol resources are reported as free
