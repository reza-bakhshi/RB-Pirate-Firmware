# Design: Verify board fault recovery and resource lifecycle

## Context and current evidence

Two physical HIL suites were skipped during host verification. Passing fake peripherals cannot establish PSU trip behavior, SD removal recovery, LCD shifter completion, SWD edge timing or mode-switch resource restoration.

Scope: Core 0 owns USB, PSU supervision, storage and test reporting. Core 1 owns protocol jobs and worker cancellation. Add reproducible board procedures and harness cases with independently observed outputs.

## Core ownership, state and failure behavior

Create the planned tools/hil fixture harness using the existing tools/serial utilities and tests/hil_test_suite.py; tools/hil does not currently exist. Avoid relying on manual claims. Emit structured case status, elapsed time, board/firmware identity, error and artifacts. Missing fixtures produce SKIP/BLOCKED and never satisfy a release gate. Inventory availability in HARDWARE.md does not establish that the session has a connected board or completed measurement.

Use externally current-limited supplies and identified resistive/electronic dummy loads for PSU cases. Preserve the board's overcurrent protection; voltage-glitch circuitry is not part of this harness. Establish safe expected voltage/current and trip limits from the board design before power tests. Use a second probe/logic analyzer for physical timing, not the firmware's own timestamps alone.

Repeat 1000 transactional mode switches across I2C, SPI, UART, 1-Wire, PS/2, SubGHz, RFID, RF24, LCDTAP and CELL, skipping explicitly unconnected modules as blocked. Include setup failure at every resource acquisition point, active-job refusal, repeated cancel, USB disconnect/reconnect and SD hotplug. Compare the complete resource and pin/clock baseline after each iteration. Capture both core heartbeats, stack high-water, watchdog resets and crash records. Normal runs must not reset; intentional stuck-core tests must reset within the configured watchdog window and preserve a valid crash record after reboot.

## Resources and rollback

Record PIO/SM, DMA, IRQ, timers, GPIO/buffer direction, clocks, SPI ownership and large-buffer ownership before and after every case. Fixtures reserve only their documented pins; unrelated debug UART pins must be excluded.

All start/failure/stop paths need an explicit rollback table before implementation. Document queue/message types, producer/consumer core, capacity, ownership-transfer point and late-reply lifetime whenever a new cross-core request is introduced. Completion callbacks must not access a caller buffer after its documented release point.

## Static memory and lifetime

Harness logs and scope captures live on the host. Firmware instrumentation uses existing fixed counters/log rings; any new metadata needs a byte budget and must not allocate in an ISR. Never place test framebuffers or long logs on the 4 KB core stacks.

## Prerequisites and exclusions

Claiming a test passed from source review, automatically energizing unidentified loads, or developing an unrelated feature.

No capability is complete merely because configuration accepts it. Hardware gates require independent evidence. New engineering choices and measured timing must be reflected in the corresponding delta scenarios before sign-off.

## Shared verification gates

All new runtime code must use static storage or bounded pools; no malloc/calloc/realloc/free/new/delete or heap-backed STL. RP2350 static main RAM must remain <=389120 bytes, including actual linked reservations measured by the existing memory guard. Baseline in the audit: 388504 bytes, leaving 616 bytes under the ceiling and 135784 bytes main-RAM headroom; scratch stacks are separately reserved at 4 KB/core. These are historical measurements, not a budget approval for future growth. Measure actual byte deltas and stack high-water after each task group.

Core 1 must not print, call FatFS or USB. Use typed bounded queues and published snapshots across cores; queue-full, disconnect, SD hotplug, timeout, cancel and watchdog behavior must be exercised where relevant. No busy wait may be unbounded. No hardware resource claims may bypass bp_res. Startup/teardown must restore leases, GPIO functions, buffer directions, IRQ handlers, clocks and PSU protection.

After each implementation task group, run `cmake --build build_rp2350 --target bus_pirate6`, the named focused suite and `bash tests/test_memory_model.sh`; record flash/RAM and warnings. Final gates: `bash tests/run_all.sh`, a separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`, zero-warning supported-platform builds and physical acceptance. Provide a reproducible wrapper/preset for optimized sanitizer flags rather than assuming scripts' existing -O1 runs satisfy -O2. Record each skipped case; a hardware skip cannot satisfy release acceptance. A build named build_rp2040 is not an RP2040 gate without verified platform identity.

## Evidence and threshold contract

Before each physical run, freeze the expected limits in its result record: PSU voltage/current envelope and maximum fault-to-disable interval, maximum control/USB response interval, ADC settling and allowed sample age, SD I/O deadline, LCD final-bit-to-release ordering, SWD clock/turnaround settings and watchdog timeout. Derive electrical limits from the identified board/fixture; lack of those limits blocks that case. Report both maximum and percentile latency over the run, not averages alone. A threshold must not be relaxed after seeing a failure without a reviewed design change and a complete rerun.

The four-hour soak must overlap continuous telemetry, USB terminal traffic, mounted storage reads/writes with independently checked content, and a representative protocol worker; include display traffic only with an identified supported panel and routing. Record each omitted workload as a coverage gap. Failure triggers include unexpected reset, resource drift, stale telemetry accepted as fresh, checksum mismatch, missed fault deadline and terminal/USB nonresponse beyond the frozen limit. Intentional reset tests run separately from this no-unexplained-reset soak.

Store host-side records with firmware commit/patch identity, board and fixture revision, SDK/compiler, effective platform, test-case ID, timestamps, expected limits, measured outcomes, status, error reason and relative paths plus SHA-256 hashes for raw serial/scope/fixture artifacts. The harness must distinguish PASS, FAIL, BLOCKED and SKIP in its exit/summary behavior. Use `SOAK_SECONDS=14400 bash tests/test_soak_runner.sh` only after checking that the backing harness runs the intended concurrent workload and has a connected fixture; increasing the duration alone is not qualification.
