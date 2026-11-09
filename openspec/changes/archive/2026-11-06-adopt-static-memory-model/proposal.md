# Proposal

## Why

Dynamic memory allocation (`malloc`, `free`, `realloc`, `calloc`, `new`, `delete`) in embedded microcontrollers introduces non-deterministic execution times, heap fragmentation, out-of-memory crashes, and hidden cross-core allocation hazards. As we build the dual-core cooperative service architecture for the RP2350 (which features 520 KB of continuous SRAM), all runtime queues, buffers, protocol contexts, and services must be statically budgeted and allocated. Moving to a strict static memory model guarantees deterministic memory consumption, prevents memory exhaustion under sustained workloads, and satisfies the prerequisite requirements for the Level 1 service kernel (B1).

## What Changes

- **Static Budgeting for RP2350**: Formally budget the RP2350's 520 KB SRAM, setting a target upper bound of <= 380 KB static RAM (.data + .bss + uninitialized sections), leaving >140 KB of contiguous SRAM headroom for scratch/burst buffers and DMA channels.
- **Zero Heap Rule in Core Runtime**: Enforce zero heap allocation (`malloc`, `free`, `realloc`, `calloc`, `new`, `delete`) in all new core runtime, service, and mode code.
- **FatFS Stack LFN Buffer**: Switch FatFS long filename support (`FF_USE_LFN`) from mode 3 (dynamic heap buffer via `ff_memalloc`/`ff_memfree`) to mode 2 (dynamic working buffer placed on the call stack), completely removing heap usage from file operations.
- **Static Pool and Arena Helpers**: Provide generic, reusable static memory pool (`bp_pool`) and linear arena (`bp_arena`) abstractions in `src/pirate/mem_pool.*` for fixed-size object allocation and transient bounded work.
- **FlatCC Static Builder Buffer**: Refactor FlatCC binary packet emission in `src/binmode/bpio.c` to use `flatcc_builder_copy_buffer` with a static output buffer, removing the per-packet `flatcc_builder_finalize_buffer` heap allocation and `free()`.
- **SubGHz Static Decoder/Encoder Pool**: Replace dynamic heap allocation in `subghz_decoder_alloc()` and `subghz_encoder_alloc()` with a static protocol context pool.
- **LCDTap Placement New and Static Framebuffer**: Replace `std::malloc` / `std::free` in `src/mode/lcdtap_backend.cpp` (`frameAlloc`, `frameFree`, `new LcdTap`) with placement new and a static framebuffer arena.
- **Heap Prohibition Guard**: Add a build check script `tests/test_memory_model.sh` and CI check to detect and prevent unauthorized heap calls in core runtime paths.
- **Diagnostic Command**: Add a `memory` CLI command (`src/commands/global/memory.c`) to inspect static SRAM budget, pool utilization, stack headroom, and heap allocation state.

## Capabilities

### New Capabilities
- `memory-model`: Deterministic static memory allocation, fixed-size object pools, static LFN and protocol buffers, and zero dynamic heap allocation in the core runtime.

### Modified Capabilities
<!-- None -->

## Impact

- **Affected Systems**:
  - `src/fatfs/ffconf.h`: Configuration of `FF_USE_LFN` to 2.
  - `src/binmode/bpio.c`: FlatCC buffer finalization.
  - `src/mode/subghz_protocols.c`: Decoder and encoder allocation.
  - `src/mode/lcdtap_backend.cpp`: Framebuffer and instance allocation.
  - `src/pirate/`: Addition of `mem_pool.h` and `mem_pool.c`.
  - `src/commands/global/`: Addition of `memory.c` and `memory.h`, registered in `src/commands.c`.
- **API Additions**:
  - `bp_pool_t`, `bp_pool_init()`, `bp_pool_alloc()`, `bp_pool_free()`.
  - `bp_arena_t`, `bp_arena_init()`, `bp_arena_alloc()`, `bp_arena_reset()`.
  - `bp_memory_stats_t`, `bp_memory_get_stats()`.
