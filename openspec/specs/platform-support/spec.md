# platform-support Specification

## Purpose
Define explicit processor/board selection, artifact identity, capability reporting and per-platform acceptance gates so unsupported hardware cannot be silently advertised as supported.

## Requirements

### Requirement: Explicit supported platform selection

Configuration SHALL honor or explicitly reject requested platform and board combinations without silently building a different processor.

#### Scenario: Unsupported request
- **WHEN** RP2040 is requested under the current RP2350-only contract
- **THEN** configuration fails with an explicit unsupported-platform message before compiling firmware.

#### Scenario: Supported artifact identity
- **WHEN** a supported firmware artifact is built
- **THEN** its platform/board identity and linker regions agree with the requested configuration and recorded release manifest.

### Requirement: Evidence-based portability claims

A platform SHALL be advertised as supported only after its own build, memory, capability and hardware acceptance gates pass.

#### Scenario: Misleading directory name
- **WHEN** a build directory is named for a processor different from its effective configuration
- **THEN** verification reports the actual configuration and cannot mark the named processor verified.

#### Scenario: Approved RP2040 port
- **WHEN** a separate RP2040 scope decision is approved
- **THEN** its own board mappings, capability fallbacks, memory limits and physical boot/protocol evidence are required before support status changes.
