# Reliability review — 2026-11-11

The changes pass host regression tests and both RP2350 build configurations.
This is not a certification that every protocol, memory path or hardware behavior is fault-free.

## Changes checked

- Configuration staging checks write lengths, sync and close results; backup/rename recovery preserves previous settings on tested failures.
- Storage RPC append uses append mode; reads honor offsets.
- Jobs stop on Core 1 before Core 0 finalizes driver cleanup. FINISHING prevents slot reuse or mode teardown before IRQ/program cleanup completes. Cancellation does not remain latched for later synchronous operations.
- Resource updates use a hardware spinlock rather than only masking local interrupts. Startup initializes it before Core 1 launches. Host stress tests check 20,000 concurrent DMA lease/release operations.
- Static pool/arena allocation rejects arithmetic overflow, invalid alignment and duplicate frees.
- MakeHex uses a bounded static parser/string/pulse workspace protected against concurrent use, validates output arguments, and rejects insufficient output capacity before copying. It no longer uses vectors or strdup. IR encode/decode cross-checks pass.
- Sub-GHz command text scratch moved off the 4 KiB stack into a static union.
- Binary TX disconnect cleanup stays with its sole USB consumer.
- CI targets RP2350 and runs regression/memory checks. Mock HIL results and unavailable physical tests are reported separately.

## Validation

| Check | Observed result |
| --- | --- |
| Host runner | 30 suites: 28 passed, 2 physical HIL suites skipped, 0 failed |
| Sanitizers | ASan/UBSan builds in applicable C/C++ suites passed |
| Explicit mock harness | 10 mode cycles and 2-second soak passed; simulation only |
| Debug firmware | Built; main RAM 367,900 bytes |
| Clean-directory Release firmware | Built; main RAM 367,916 bytes |
| Static RAM budget | 389,120 bytes; Release margin 21,204 bytes |
| Scratch stacks | Separate 4 KiB regions for each core, linked successfully |
| Release stack instrumentation | Sub-GHz send frame 672 bytes (was 3,264); MakeHex generation frame 712 bytes |
| Syntax/configuration checks | Python syntax, workflow YAML and git diff whitespace checks passed |

The Release build used GCC stack-usage output (`-fstack-usage`). Per-function frames
are not a worst-case call-chain or interrupt nesting measurement.

Reproduce the normal checks after configuring the Pico SDK:

```sh
cmake --build build_rp2350 --target bus_pirate6 --parallel
bash tests/run_all.sh
bash tests/test_memory_model.sh
python3 tests/hil_test_suite.py --mock --cycles 10 --soak 2
```

`BP_TEST_BUILD_DIR` can select a different configured build for the memory guard.

## Remaining limits

- No physical port was selected for final validation. No flashing or electrical protocol verification was performed. Production sniffer teardown, PSU faults, USB/MSC concurrency and protocol timing still need physical tests.
- USB, monitoring and protocol jobs still share Core 1. A blocking slice can delay those services; complete control/data isolation is unfinished.
- Legacy FlatCC default builder/emitter paths still reference heap allocators. Global zero-heap operation and live heap usage are not established by the scoped memory guard.
- Stack high-water behavior, combined call chains, interrupt nesting and long-duration hardware stability remain unmeasured.
- FAT backup/rename is recoverable sequencing, not protection against every form of power-loss filesystem corruption.

See [testing instructions](../testing/testing_guide.md) and
[current core ownership](../architecture/dual_core_guide.md) before hardware qualification.
