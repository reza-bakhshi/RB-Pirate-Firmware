# Testing the firmware

Build the RP2350 firmware first, then run the regression suite:

```sh
cmake --build build_rp2350 --target bus_pirate6 --parallel
bash tests/run_all.sh
```

The runner reports passed, failed and skipped suites separately. Hardware suites return
77 when no port is selected. The memory guard returns 77 without a firmware build;
CI creates the build before running it. Other failures remain failures.
Most C suites run normal and ASan/UBSan builds. Sanitizers do not prove peripheral timing.

## Physical device checks

Select the intended device explicitly:

```sh
HIL_PORT=/dev/ttyACM0 bash tests/test_hil_smoke.sh
python3 tests/hil_test_suite.py --port /dev/ttyACM0 --cycles 1000 --soak 3600
```

Install pyserial in the test environment. `--auto-detect` opts into discovery, which can
match unrelated RP2350 devices. Connection failures never fall back to simulation.
Mode cycling uses named modes with speed flags to avoid interactive configuration;
it checks the resulting prompt. Soak checks numeric heartbeats, watchdog feed progress,
stack margin and counter drift. The Ctrl+C check runs at idle; it does not prove an
active capture was cancelled. CDC1 stress and electrical protocol validation are separate
hardware tasks and are not implemented by this harness.

## Testing the harness without hardware

```sh
python3 tests/hil_test_suite.py --mock --cycles 10 --soak 2
bash tests/test_hil_harness.sh
```

Mock results are labeled `MOCK HARNESS`, never physical HIL. The harness unit tests check
skip behavior, explicit mock labeling, incorrect prompts, missing telemetry and stalled counters.
The sniffer resource tests model peripheral state and exercise the production scheduler;
they do not execute complete production drivers or establish physical PIO teardown correctness.

## Storage and job regression coverage

`test_storage_service.sh` compiles the production service with a stateful filesystem fake.
It checks appended/read-back bytes, read offsets, write/short-write/sync/close/rename failures,
preservation of previous settings and backup recovery. The fake is test-only.
`test_job.sh` checks cleanup exactly once for queued/running cancellation, deadlines, success
and errors, including that the job slot remains unavailable until Core 0 acknowledges deferred cleanup.

## Core ownership and FlatCC allocation

`test_control_service.sh` tests the actual dispatcher and job engine with threads.
Control polling continues with a blocked worker, rejects wrong-core entry, suppresses
recursion, and returns results after 1,000 worker transactions. Kernel tests also
verify control hooks run only from `bp_service(0)`.

`test_flatcc_memory.sh` compiles real schema-generated builders and the FlatCC runtime
with embedded allocation hooks under normal builds and ASan/UBSan. It tests 1,000
512-byte responses, status vectors, aligned finalization, pool exhaustion, realloc
preservation, repeated setup/clear, oversized requests and Core 1 rejection. The
memory-model guard scans all FlatCC runtime backend objects for libc heap references.

These tests validate the ownership and allocation implementation. Physical USB/MSC,
protocol timing, PSU trips and watchdog recovery still require a selected device.
