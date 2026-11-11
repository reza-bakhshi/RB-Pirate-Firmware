# Static Memory Model

> Hard rule: **new code must not use `malloc`, `calloc`, `realloc`, `free`, `new` or `delete`.**
> Use static storage, fixed pools, bump arenas or ring buffers instead.

---

## Current verification limits

The 380 KiB link-time budget covers static main RAM, including reserved heap and alignment.
It does not measure live heap or worst-case combined call stacks. FlatCC runtime allocation, including builder, emitter, refmap, JSON backends and
aligned finalization, uses bounded static block pools. Other linked library paths
are not globally instrumented; the scoped symbol guard is not a whole-firmware
live-heap measurement. MakeHex now uses bounded static workspace
with a single-user guard instead of vectors/strdup and large stack scratch.

## 1. Why

The firmware is a long-running embedded device with two cores and timing-sensitive I/O. A general
heap causes:

- fragmentation over hours/days of uptime,
- unpredictable allocation latency,
- failure paths that are hard to test,
- hidden allocations inside third-party code,
- cross-core heap races and lock contention,
- silent corruption when a 128 KB arena and the heap compete for the same RAM.

Static allocation makes memory use visible at link time and makes failure a boot-time configuration
problem, not a runtime surprise.

---

## 2. Allowed allocation mechanisms

| Mechanism | Use |
|---|---|
| **Static arrays** | Fixed-size buffers, queues, per-service state |
| **Fixed block pool** | `bp_pool_t`: N blocks of fixed size, acquire/release tokens |
| **Bump arena** | `bp_arena_t`: allocate during a job, reset the whole arena at job end |
| **SPSC ring** | byte/word streams between services or cores |
| **Placement new** | C++ objects constructed into static storage, no heap |
| **Linker section** | large immutable tables, font data, protocol tables |

Forbidden in new code:

- `malloc`, `calloc`, `realloc`, `free`
- `new`, `delete`, `new[]`, `delete[]`
- `std::vector`, `std::string`, `std::map`, `std::function` and other heap-using STL types
- Variable-length arrays
- Allocation from an ISR

---

## 3. Pool design

```c
typedef struct {
    uint8_t *storage;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t *free_bitmap;   // or a free-list of indexes
    const char *owner;
} bp_pool_t;

void *bp_pool_acquire(bp_pool_t *pool);        // NULL if exhausted
void bp_pool_release(bp_pool_t *pool, void *p);
uint32_t bp_pool_free(const bp_pool_t *pool);
```

Rules:

- A pool is created at boot with a static backing array.
- Acquire/release is not `malloc`; it returns a block from the bitmap/free-list.
- Exhaustion is a reported error, not a crash.
- Pools are single-owner or protected; no cross-core pool without a lock-free queue.

## 4. Arena design

```c
typedef struct {
    uint8_t *base;
    size_t capacity;
    size_t used;
    const char *owner;
} bp_arena_t;

void *bp_arena_alloc(bp_arena_t *arena, size_t size, size_t align);
void bp_arena_reset(bp_arena_t *arena);
```

Rules:

- Use for job-scoped temporary data (parsing, file names, decoded protocol state).
- Reset at job end, not per object.
- No `free`; the arena is monotonic.
- If a request does not fit, return NULL and let the caller report a clean failure.

---

## 5. Existing heap sites and migration

These are the known heap users. New code must not add more.

| Site | Current use | Target |
|---|---|---|
| `src/fatfs/ffconf.h` | `FF_USE_LFN 3` allocates per file operation | set to `2` (static LFN buffer) |
| `src/fatfs/ffsystem.c` | `ff_memalloc` / `ff_memfree` | not used after LFN=2 |
| `src/binmode/bpio.c` | Fixed output buffer, Core 0 builder | migrated |
| FlatCC runtime (`builder`, `emitter`, `refmap`, JSON) | 8 x 256, 4 x 1024, 2 x 4096 byte fixed blocks | migrated via `bp_flatcc_memory` |
| `src/mode/subghz_protocols.c` | decoder/encoder `malloc`/`free` | fixed protocol pool sized for max concurrent decoders |
| `src/mode/subghz_rx.c` | decoder slots per protocol | static slot array |
| `src/mode/lcdtap_backend.cpp` | `new LcdTap`, `std::malloc` framebuffer | placement new into static storage + framebuffer arena |
| `src/lib/kilo/*` | editor rows, filenames, buffers | `bp_arena` / fixed editor pool |
| `src/lib/hx/*` | undo actions, editor buffers | `bp_arena` / fixed editor pool |
| `src/lib/pico-i2c-sniff/ram_fifo.c` | capture buffer `malloc` | static capture pool |
| `src/lib/ap33772s/*` | example/test allocations | not compiled in firmware |

---

## 6. Sizing and measurement

Before replacing a heap site, measure:

- peak live bytes,
- number of concurrent objects,
- largest single allocation,
- worst-case lifetime.

Then size a static pool with headroom. Record the numbers in the change's design.md.

At link time:

- `arm-none-eabi-size` and the linker map show static usage.
- `nm` can confirm whether `malloc`/`free` are still referenced.
- A debug build may wrap `malloc`/`free` with a trap to catch accidental new uses.

---

## 7. Enforcement

- `openspec/config.yaml` forbids dynamic allocation in new code.
- A CI grep rejects `malloc(`, `calloc(`, `realloc(`, `free(`, `new `, `delete ` in `src/kernel/`,
  `src/services/` and new mode files, with an explicit allowlist for third-party code.
- Code review checks that every buffer has a static owner and a documented size.
- A change that needs dynamic allocation must first add a memory-model exception with a measured
  budget and an owner.

## FlatCC pool ownership and limits

The pools reserve 14 KiB of payload and are owned by Core 0. They replace malloc,
calloc, realloc, free, aligned_alloc and aligned_free through `flatcc_alloc.h` when
`BP_EMBEDDED` is defined. Unsupported sizes or exhausted pools return failure; failed
realloc preserves the original block. Block addresses remain stable until release.
The builder returns allocation errors rather than asserting on capacity exhaustion;
structural FlatCC assertions remain enabled. Callers must check returned buffers.

BPIO limits writes/reads to 512 bytes and responses to its packet buffer. Setup clears
an existing builder before reinitialization. Host tests build maximum-data responses,
mode/pin-label status vectors, aligned final buffers and oversized requests repeatedly.
The object-symbol guard rejects libc allocation references in every FlatCC runtime backend.
