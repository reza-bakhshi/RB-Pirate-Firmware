# Design

## Context

The Bus Pirate 6 firmware runs on the RP2350B, which provides 520 KB of continuous SRAM plus two dedicated 4 KB zero-wait-state scratch banks (`SCRATCH_X` and `SCRATCH_Y`). Historically, components of the codebase (such as FatFS long filename support, SubGHz protocol decoders/encoders, FlatCC binary serialization, and LCDTap framebuffers) used standard C library dynamic heap allocation (`malloc`, `free`, `realloc`, `calloc`, `new`, `delete`).

In an embedded dual-core system, heap allocation introduces fragmentation, unbounded execution latency, non-deterministic failure modes, and cross-core synchronization hazards. Transitioning to a strict static memory model is a prerequisite for Level 1 (specifically B1 `add-service-kernel` and B2 `add-protocol-job-runtime`).

## Goals / Non-Goals

**Goals:**
- Target exclusively RP2350B (520 KB SRAM).
- Establish a static RAM budget with <= 380 KB total static consumption (.data + .bss + uninitialized sections), leaving >140 KB of contiguous headroom for scratch buffers, DMA FIFOs, and stacks.
- Enforce a "zero dynamic heap allocation" rule in all core runtime, service, and mode execution paths.
- Reconfigure FatFS (`FF_USE_LFN = 2`) to allocate long filename buffers on the stack, eliminating `ff_memalloc` and `ff_memfree`.
- Replace dynamic allocations in SubGHz with a statically sized protocol context pool.
- Refactor FlatCC binary packet emission in `bpio.c` to use `flatcc_builder_copy_buffer` into a static buffer, removing per-packet `malloc` and `free`.
- Replace LCDTap dynamic object and framebuffer allocations with placement new and a pre-budgeted static frame buffer.
- Provide generic static memory pool (`bp_pool`) and linear arena (`bp_arena`) abstractions for upcoming services and jobs.
- Provide a `memory` CLI command to display live static memory consumption, pool utilization, and stack watermarks.
- Implement an automated symbol check script to guarantee no unauthorized heap callers exist in runtime builds.

**Non-Goals:**
- Removing the 128 KB shared capture buffer (`BIG_BUFFER`) at this stage (Phase B3 will transition this to named leases).
- Rewriting third-party standalone tools (e.g. host test fixtures or tools outside the firmware binary).

## Decisions

### 1. Dedicated Scratch Banks for Core Stacks
- **Decision**: Keep Core 0 stack in `SCRATCH_Y` (0x20081000, 4 KB) and Core 1 stack in `SCRATCH_X` (0x20080000, 4 KB).
- **Rationale**: The RP2350 architecture provides single-cycle access to these scratch banks without contending with DMA or PIO bus masters accessing main SRAM banks. Watermark monitoring (`0x55AA55AA`) shows typical stack usage remains under 2 KB even during deep command processing.

### 2. FatFS LFN Mode 2 (Stack Working Buffer)
- **Decision**: Change `FF_USE_LFN` from 3 (heap) to 2 (stack) in `src/fatfs/ffconf.h`.
- **Rationale**: Mode 2 allocates the ~512-byte LFN working buffer directly on the call stack only for the duration of the filesystem call. Because Core 0 owns all filesystem operations, and its stack headroom exceeds 2.5 KB, this completely eliminates heap allocations with zero static memory overhead.

### 3. Static Protocol Pool for SubGHz
- **Decision**: In `src/mode/subghz_protocols.c`, implement a fixed static pool of decoder and encoder instances (`subghz_decoder_pool` and `subghz_encoder_pool`).
- **Rationale**: Inspection reveals that all 25 decoder private structs are under 128 bytes, and only 1 to 2 decoders/encoders are ever active simultaneously. A pre-allocated pool of 4 decoder slots (160 bytes each) and 2 encoder slots (128 bytes each) totals < 1 KB of static RAM and eliminates `malloc`/`free` during radio reception.

### 4. FlatCC Direct Copy Buffer
- **Decision**: In `src/binmode/bpio.c`, replace `flatcc_builder_finalize_buffer` with `flatcc_builder_copy_buffer` targeting a static packet buffer (`bpio_packet_buf[BPIO_MAX_PACKET_SIZE]`).
- **Rationale**: `finalize_buffer` allocates a new heap block per packet and requires `free()`. `copy_buffer` writes directly into existing static memory with zero allocation overhead and identical wire serialization.

### 5. LCDTap Placement New and Static Framebuffer
- **Decision**: In `src/mode/lcdtap_backend.cpp`, replace `new lcdtap::LcdTap` with placement new inside an aligned static buffer `gTapStorage`, and provide a static frame buffer for `frameAlloc` instead of `std::malloc`.
- **Rationale**: LCDTap is a singleton mode. Dynamic allocation of the C++ class and framebuffer was unnecessary and created potential memory leaks on abnormal mode termination.

### 6. Generic Pool & Arena Helpers (`src/pirate/mem_pool.*`)
- **Decision**: Implement `bp_pool_t` (intrusive singly-linked free-list over fixed-size static blocks) and `bp_arena_t` (linear bump allocator with rewind/reset).
- **Rationale**: Future changes (B1 service kernel, B2 job runtime) require allocating small events, timers, and job descriptors. Having standardized, zero-heap pool primitives prevents ad-hoc buffer proliferation.

### 7. Automated Heap Prohibition Guard
- **Decision**: Provide `tests/test_memory_model.sh` which analyzes the symbol table of `bus_pirate6.elf` to ensure undefined heap symbols or calls from runtime objects are strictly rejected.

## Risks / Trade-offs

- **[Risk] FatFS LFN on stack increases call frame depth**
  $\to$ **Mitigation**: Verified Core 0 stack watermark. Peak observed stack depth remains well below the 4 KB boundary. Stack watermarks are checked continuously by the `health` and `memory` commands.
- **[Risk] Fixed SubGHz pool limits simultaneous decoders**
  $\to$ **Mitigation**: SubGHz protocols only decode one transmission at a time (or sequential protocol auto-probing). Sizing the pool to 4 concurrent slots provides ample headroom for multi-protocol sniffing without heap fragmentation.
