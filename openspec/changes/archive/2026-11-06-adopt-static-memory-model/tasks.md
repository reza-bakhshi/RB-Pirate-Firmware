# Tasks

## 1. FatFS & SubGHz Heap Elimination

- [x] 1.1 Set `FF_USE_LFN` to `2` in `src/fatfs/ffconf.h`, verify build and confirm `ff_memalloc` and `ff_memfree` are no longer compiled or linked.
- [x] 1.2 Implement a static decoder and encoder context pool in `src/mode/subghz_protocols.c`, replacing `malloc` and `free` in `subghz_decoder_alloc/free` and `subghz_encoder_alloc/free`.
- [x] 1.3 Run `tests/test_subghz.sh` to verify all 45 SubGHz protocols pass with the static protocol pool under ASan and UBSan.

## 2. FlatCC & LCDTap Heap Elimination

- [x] 2.1 Refactor packet emission in `src/binmode/bpio.c` to copy finalized FlatCC buffers directly into a static output buffer, removing `free(buf)`.
- [x] 2.2 Replace dynamic heap allocations (`new`, `delete`, `std::malloc`, `std::free`) in `src/mode/lcdtap_backend.cpp` with placement new and a static frame buffer.

## 3. Static Pool and Arena Abstractions

- [x] 3.1 Implement fixed-block static pool `bp_pool_t` in `src/pirate/mem_pool.h` and `src/pirate/mem_pool.c` with O(1) alloc/free and zero fragmentation.
- [x] 3.2 Implement linear bump allocator `bp_arena_t` in `src/pirate/mem_pool.h` and `src/pirate/mem_pool.c` with reset capability.
- [x] 3.3 Create host unit test `tests/test_mem_pool.c` and runner `tests/test_mem_pool.sh` and verify all pool/arena operations pass under ASan and UBSan.

## 4. Memory Diagnostic Command

- [x] 4.1 Implement `memory` CLI command in `src/commands/global/memory.c` and `memory.h` reporting SRAM budget, static RAM utilization, stack headroom, and pool metrics.
- [x] 4.2 Register `memory` in `src/commands.c` under `CMD_CAT_SYSTEM` and add source files to `src/CMakeLists.txt`.

## 5. Heap Prohibition Guard & Verification

- [x] 5.1 Create `tests/test_memory_model.sh` to verify that `bus_pirate6.elf` and runtime modules do not invoke dynamic heap allocators.
- [x] 5.2 Build `bus_pirate6` on `build_rp2350`, record flash and static RAM usage, and verify static RAM consumption remains below the 380 KB budget.
- [x] 5.3 Run the full host test suite (`test_terminal_tx.sh`, `test_spsc_queue.sh`, `test_ir_protocols.sh`, `test_subghz.sh`, `test_crash_record.sh`, and `test_mem_pool.sh`).
- [x] 5.4 Run `npx @fission-ai/openspec validate --all --strict` and verify all checks pass.

## Workflow follow-up

- Review implementation with developer.
- Archive this change with `openspec archive adopt-static-memory-model` once all tasks and verification pass.
- Proceed to B1 (`add-service-kernel`).
