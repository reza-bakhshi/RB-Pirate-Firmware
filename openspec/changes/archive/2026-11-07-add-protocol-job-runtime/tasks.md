# Tasks

## 1. Job Runtime Primitives and Core Execution

- [x] 1.1 Implement `src/kernel/bp_job.h` defining job states, descriptor `bp_job_t`, progress/output envelopes, and API signatures.
- [x] 1.2 Implement `src/kernel/bp_job.c` with static job context, submit, cancel, deadline checking, progress reporting, and Core 1 execution slice.
- [x] 1.3 Integrate job servicing into `bp_service(1)` in `src/kernel/bp_kernel.c` and connect Core 1 execution.

## 2. Unit Testing and Build Integration

- [x] 2.1 Add host unit test suite `tests/test_job.c` and `tests/test_job.sh` covering submission, progress streaming, deadline timeout, and cancellation latency <100ms.
- [x] 2.2 Add `src/kernel/bp_job.c` to `src/CMakeLists.txt` and verify target builds and passes `tests/test_memory_model.sh`.

## 3. CLI Command and Hardware Integration

- [x] 3.1 Implement `src/commands/global/cmd_job.h` and `src/commands/global/cmd_job.c` providing `job run [ms]`, `job status`, and `job cancel` commands.
- [x] 3.2 Register `job` command in `src/commands.c` and `src/CMakeLists.txt`.
- [x] 3.3 Compile `bus_pirate6.uf2` and verify all host unit test suites pass with zero runtime heap usage.
