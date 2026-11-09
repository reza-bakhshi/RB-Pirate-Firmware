# command-parser Specification

## Purpose
Defines normative requirements for syntax token compilation, buffer bounds checking, repeat count limits, command lookup, argument parsing safety, and fuzz robustness.

## Requirements

### Requirement: Syntax Token Buffer Bounds Enforcement
The syntax compiler SHALL verify that the output bytecode count does not exceed `SYN_MAX_LENGTH` before writing any token into the bytecode array. Any input exceeding buffer capacity SHALL be rejected with an explicit error without memory corruption.

#### Scenario: Input exceeding maximum bytecode slots is rejected
- **WHEN** user input generates more bytecode commands than `SYN_MAX_LENGTH`
- **THEN** the compiler aborts compilation, leaves buffers intact, and displays a capacity error.

### Requirement: Repeat Count Syntax Validation
The syntax compiler SHALL reject a repeat attribute of zero (`:0`) as an invalid syntax construct. Repeat counts SHALL be strictly greater than zero and bounded by the remaining capacity of the input/output bytecode structures.

#### Scenario: Repeat of zero is rejected
- **WHEN** a syntax token contains a repeat modifier of `:0`
- **THEN** compilation fails with an error indicating repeat count cannot be zero.

#### Scenario: Valid repeat count is accepted
- **WHEN** a syntax token contains a positive repeat modifier within bounds
- **THEN** the bytecode token is populated with the specified repeat count.

### Requirement: Malformed and Fuzzed Input Tolerance
The command and syntax parsers SHALL handle arbitrary malformed, truncated, nested, or randomized byte sequences without crashing, hanging the CPU, overflowing internal buffers, or triggering an unhandled watchdog reset.

#### Scenario: Malformed syntax string during fuzzing
- **WHEN** random or malformed byte sequences are fed to the parser
- **THEN** the parser returns an error status cleanly without memory leaks or assertions.

### Requirement: Unified Command Descriptors
All system and global commands SHALL provide a valid `bp_command_def_t` descriptor specifying command syntax, argument constraints, help descriptions, and Hiz-mode permissions.

#### Scenario: Command lookup through unified table
- **WHEN** a command line is executed by the user
- **THEN** the parser resolves the command against the unified table and enforces Hiz mode rules and argument requirements.
