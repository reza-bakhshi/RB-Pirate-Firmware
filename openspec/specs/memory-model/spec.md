# memory-model Specification

## Purpose
Establishes a deterministic static memory model for the RP2350 platform, eliminating heap allocation from the core runtime and replacing dynamic allocations with statically budgeted pools and arenas.

## Requirements

### Requirement: Static memory budget
The firmware SHALL allocate all operational memory from static storage (.data, .bss, uninitialized sections, or scratch banks) within a maximum static budget of 380 KB on the RP2350, preserving at least 140 KB of contiguous SRAM headroom.

#### Scenario: Firmware static image link
- **WHEN** the firmware is compiled and linked for target `bus_pirate6`
- **THEN** total static RAM usage (.data + .bss + uninitialized) does not exceed 380 KB
- **AND** dedicated scratch banks SCRATCH_X and SCRATCH_Y contain the Core 1 and Core 0 stacks

### Requirement: Zero heap allocation in core runtime
The firmware core runtime, services, and protocol drivers SHALL execute without calling dynamic heap allocators (`malloc`, `free`, `realloc`, `calloc`, `new`, `delete`).

#### Scenario: Core runtime execution
- **GIVEN** normal operation of the command prompt, terminal, and peripheral protocols
- **WHEN** commands or continuous protocol transactions execute
- **THEN** no heap allocation requests are dispatched to the C runtime heap

#### Scenario: File system operations
- **GIVEN** FatFS is configured with long filename support
- **WHEN** file paths with long filenames are opened, read, written, or created
- **THEN** the LFN working buffer is allocated on the function call stack without invoking `ff_memalloc` or heap allocation

### Requirement: Static memory pools and arenas
The firmware SHALL provide static pool and arena management for fixed-size and bounded transient objects.

#### Scenario: Fixed-size block allocation
- **GIVEN** a static memory pool initialized with a fixed block size and capacity
- **WHEN** an object is allocated from the pool
- **THEN** the pool returns a pointer to an available block in O(1) time without fragmentation
- **AND** returns NULL deterministically when capacity is reached

#### Scenario: SubGHz and protocol allocation
- **WHEN** a SubGHz protocol decoder or encoder is instantiated
- **THEN** its context is allocated from a pre-budgeted static protocol buffer without heap calls

#### Scenario: Binary packet serialization
- **WHEN** FlatCC serializes binary protocol messages in BPIO mode
- **THEN** the payload is copied directly into a pre-allocated static buffer without dynamic buffer finalization

### Requirement: Memory diagnostic command
The firmware SHALL provide a CLI command to report static memory budget, pool utilization, stack high-water, and heap state.

#### Scenario: Inspect memory statistics
- **WHEN** the user executes the `memory` command at the terminal
- **THEN** the firmware prints the total SRAM budget, static RAM utilization, stack watermarks, and pool status
