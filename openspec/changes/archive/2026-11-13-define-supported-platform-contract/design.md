# Design: Define and enforce the supported platform contract

## Context and current evidence

Top-level CMake forces RP2350 and both existing build directories cache rp2350-arm-s. A directory named build_rp2040 is therefore not RP2040 evidence, while the project OpenSpec context explicitly excludes BP5/RP2040.

Scope: Build configuration and product capability reporting. Record a product decision: enforce the current RP2350-only scope, or approve a separately scoped RP2040 port before changing the support claim. Until that decision, RP2350-only remains the effective contract.

## Core ownership, state and failure behavior

Add a decision record with rationale and impacted commands/dependencies. For RP2350-only, reject an explicit unsupported -DPICO_PLATFORM=rp2040 or incompatible board request rather than overriding it silently; diagnostics explain how to select supported hardware. Align config.yaml, README, board headers, presets, CI and release names. Build-directory names never prove silicon.

If a port is approved later, create separate real board/platform configuration, SDK libraries and link maps. Inventory SHA hardware vs software fallback, pico/rand entropy behavior, absent HSTX, PIO count, GPIO width, SWD clocks, watchdog and storage/display pin mapping. Unsupported capabilities must return clear errors; software substitutes require reference vectors and failure tests. Verify actual compile definitions/ELF machine attributes/linker memory regions, then boot representative hardware. Shared protocol APIs must preserve wire behavior while hardware-specific capabilities differ.

Define a machine-readable build identity containing platform, board revision, SDK/toolchain, feature flags and memory-region summary for artifacts/HIL evidence. Bind firmware-info output and artifact naming to that identity. Document provenance of existing misleading cache directories without deleting user build outputs.

## Resources and rollback

Document platform-specific available PIO, DMA, IRQ, GPIO, timers and accelerator counts. Resource limits and pin mapping must derive from the selected hardware, not an RP2350 assumption.

All start/failure/stop paths need an explicit rollback table before implementation. Document queue/message types, producer/consumer core, capacity, ownership-transfer point and late-reply lifetime whenever a new cross-core request is introduced. Completion callbacks must not access a caller buffer after its documented release point.

## Static memory and lifetime

RP2350 static main RAM remains <=389120 bytes with explicit stack/scratch accounting. An RP2040 port must define its own linker-region/static ceiling and stack headroom before feature selection; the 512 KB-based budget is not portable. New platform manifests should be compile-time data with measured retention.

## Prerequisites and exclusions

Silently approving an RP2040 port, claiming RP2040 compatibility from directory names, or requiring the RP2350 RAM budget to fit RP2040.

No capability is complete merely because configuration accepts it. Hardware gates require independent evidence. New engineering choices and measured timing must be reflected in the corresponding delta scenarios before sign-off.

## Shared verification gates

All new runtime code must use static storage or bounded pools; no malloc/calloc/realloc/free/new/delete or heap-backed STL. RP2350 static main RAM must remain <=389120 bytes, including actual linked reservations measured by the existing memory guard. Baseline in the audit: 388504 bytes, leaving 616 bytes under the ceiling and 135784 bytes main-RAM headroom; scratch stacks are separately reserved at 4 KB/core. These are historical measurements, not a budget approval for future growth. Measure actual byte deltas and stack high-water after each task group.

Core 1 must not print, call FatFS or USB. Use typed bounded queues and published snapshots across cores; queue-full, disconnect, SD hotplug, timeout, cancel and watchdog behavior must be exercised where relevant. No busy wait may be unbounded. No hardware resource claims may bypass bp_res. Startup/teardown must restore leases, GPIO functions, buffer directions, IRQ handlers, clocks and PSU protection.

After each implementation task group, run `cmake --build build_rp2350 --target bus_pirate6`, the named focused suite and `bash tests/test_memory_model.sh`; record flash/RAM and warnings. Final gates: `bash tests/run_all.sh`, a separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`, zero-warning supported-platform builds and physical acceptance. Provide a reproducible wrapper/preset for optimized sanitizer flags rather than assuming scripts' existing -O1 runs satisfy -O2. Record each skipped case; a hardware skip cannot satisfy release acceptance. A build named build_rp2040 is not an RP2040 gate without verified platform identity.
