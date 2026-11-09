# Proposal: Split Config, Runtime, and UI Snapshot

## Why
The legacy `system_config` structure conflates three distinct classes of state into a single global struct: persisted configuration, volatile control-plane runtime state, and dynamic pin measurements displayed by toolbars. When Core 1 runs protocol jobs or toolbars poll analog pins, unprotected reads of `system_config` risk data races and tearing. Splitting `system_config` into persisted config with schema versioning, volatile runtime state owned by Core 0, and a seqlock-protected atomic UI snapshot (`bp_ui_snapshot_t`) enables lockless status display without data races.

## What Changes
- Introduce `bp_config_t` for persisted user settings with schema versioning and validation.
- Introduce `bp_runtime_t` for Core 0 volatile control plane state.
- Introduce `bp_ui_snapshot_t` with sequence counter (seqlock) for atomic, lockless reads by Core 1 and UI toolbars.
- Add configuration migration and fallback to defaults on corrupt config files.
- Provide host test suite verifying seqlock consistency under concurrency, corrupt config recovery, and migration.

## Capabilities
### New Capabilities
- `configuration`: Formalized persisted configuration schema, migration chain, and seqlock UI snapshot.

### Modified Capabilities
None.

## Impact
- Core 0 exclusively mutates runtime and configuration.
- Core 1 and UI toolbars read immutable snapshots without spinning or locking.
- RAM/Flash footprint remains well within 380 KB ceiling.
