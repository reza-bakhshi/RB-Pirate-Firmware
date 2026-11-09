# Tasks

## 1. Resource Manager Core Implementation

- [x] 1.1 Implement `src/kernel/bp_resource.h` defining resource classes, owner IDs, lease types, and API declarations (`bp_res_init`, `bp_res_acquire`, `bp_res_release`, `bp_res_release_all`, `bp_res_claim_unused_pio_sm`, `bp_res_claim_unused_dma_ch`, `bp_res_acquire_irq`, `bp_res_acquire_buffer`).
- [x] 1.2 Implement `src/kernel/bp_resource.c` with static registry tables, concurrency guards, hardware check sync, conflict rejection, and transactional cleanup.
- [x] 1.3 Refactor `src/pirate/mem.c` to route through `bp_resource` for big-buffer leasing and remove `printf()` from allocation failure paths.
- [x] 1.4 Add `bp_res_init()` invocation in `src/pirate.c` during system initialization.

## 2. CLI Command and Subsystem Integration

- [x] 2.1 Implement `src/commands/global/cmd_res.h` and `src/commands/global/cmd_res.c` providing the `res` CLI command to display resource lease status.
- [x] 2.2 Register `res` command in `src/commands.c` and add sources to `src/CMakeLists.txt`.
- [x] 2.3 Ensure mode error paths and teardown invoke `bp_res_release_all(owner)`.

## 3. Verification and Testing

- [x] 3.1 Implement host unit test suite `tests/test_resource.c` and `tests/test_resource.sh` validating acquire, release, conflict rejection, release-all, and a 1000 mode-switch soak test with ASan/UBSan.
- [x] 3.2 Verify all test suites pass, static RAM remains <= 380 KB with zero heap, and build target `bus_pirate6.uf2` compiles cleanly.
