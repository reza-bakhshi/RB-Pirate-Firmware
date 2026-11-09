# Design: Route Terminal I/O Through Control Core

## Context

In the dual-core architecture, Core 0 (CTRL) owns the user interface, command interpreter, filesystem, and configuration, while Core 1 (PROTO) owns hardware protocol engines and hardware capture. Previously, both cores wrote directly to console streams (`printf` calling `tx_fifo_put`), causing potential race hazards, corrupted escape sequences, and lock contention. Furthermore, input from the terminal was received blindly into `rx_fifo` without awareness of whether Core 1 was running a protocol job.

Step B4 introduces the `bp_terminal` service to enforce Core 0 ownership of terminal I/O, isolate Core 1 output through a dedicated lock-free SPSC text ring, and demultiplex input between command prompt, job input, and Ctrl+C cancellation.

## Goals / Non-Goals

**Goals:**
- Provide a clean, unified terminal service API (`src/services/bp_terminal.h`, `src/services/bp_terminal.c`) owned by Core 0.
- Eliminate cross-core `printf` collisions by redirecting Core 1 `printf` / `_putchar` to a lock-free SPSC ring (`s_core1_text_ring`, 2048 bytes).
- Core 0 drains `s_core1_text_ring` during `bp_service(0)` and emits characters to the console in strict FIFO order.
- Demultiplex incoming terminal characters on Core 0:
  - Intercept Ctrl+C (`0x03`) to immediately cancel running protocol jobs without forwarding `0x03` to the job.
  - Forward user data keystrokes to `s_core1_job_input_ring` (256 bytes) when an active job is executing.
  - Direct keystrokes to the terminal shell when idle.
- Zero dynamic heap allocation (`malloc`, `free`, `new`, `delete`).

**Non-Goals:**
- Moving CDC1 binary mode data to queues (deferred to B6 `move-binary-modes-to-transport-queues`).
- Full mode lifecycle migration (deferred to B7 `split-control-and-protocol-cores`).

## Decisions

### 1. Centralized Terminal Service Architecture
- **Decision**: Define `src/services/bp_terminal.h` as the single API for console I/O on Core 0:
  ```c
  bp_status_t bp_term_init(void);
  bp_status_t bp_term_write(const char* buf, uint32_t len);
  bp_status_t bp_term_putc(char c);
  bp_status_t bp_term_getc(char* c);
  bp_status_t bp_term_core1_putc(char c);
  bp_status_t bp_term_job_read(char* buf, uint32_t max_len, uint32_t* out_len);
  void        bp_term_service(void);
  bool        bp_term_wait_drain(uint32_t timeout_ms);
  ```
- **Rationale**: Isolates legacy USB FIFO drivers behind a stable service layer and prepares the codebase for clean core separation in Phase C.

### 2. Lock-Free Core 1 Text Streaming Ring
- **Decision**: Statically allocate an SPSC ring (`s_core1_text_ring`, 2048 bytes) for Core 1 text.
- **Rationale**: Core 1 can safely call `printf()` without blocking, acquiring mutexes, or contending with Core 0. `bp_term_service()` on Core 0 reads from this ring and forwards characters to the terminal TX FIFO.

### 3. Per-Core `_putchar` Dispatch
- **Decision**: In `src/printf-4.0.0/printf.c`:
  ```c
  void _putchar(char character) {
  #if defined(HAVE_PICO_SDK)
      if (get_core_num() == 1) {
          (void)bp_term_core1_putc(character);
          return;
      }
  #endif
      (void)bp_term_putc(character);
  }
  ```
- **Rationale**: Seamlessly routes all `printf` calls from either core to the appropriate queue with zero changes to existing `printf` call sites.

### 4. Input Demultiplexing & Ctrl+C Handling
- **Decision**: Core 0 processes inbound characters in `bp_term_service()`:
  - If character is `0x03` (Ctrl+C) and `bp_job_get_active_id() > 0`:
    Core 0 immediately triggers `bp_job_cancel()`. The character is consumed and not passed to the job or terminal prompt.
  - If a protocol job is active and has requested input:
    The character is pushed to `s_core1_job_input_ring`.
  - Otherwise:
    The character remains in the terminal input queue for the shell editor.
- **Rationale**: Provides instant `< 100 ms` cancellation latency while preventing Ctrl+C from polluting job input buffers.

## Risks / Trade-offs

- **[Risk] High-volume Core 1 logs overflowing text ring**: If Core 1 emits copious debug messages faster than Core 0 drains, characters could be dropped.
  - *Mitigation*: 2048-byte ring with atomic drop counter. Core 0 emits a dropped-bytes notification if overflow occurs.
- **[Risk] Text interleaving with active command prompt**: Core 1 text arriving while the user is typing at the prompt could scramble prompt line editing.
  - *Mitigation*: Core 0 terminal service drains Core 1 output before redrawing prompts or linenoise buffers.

## Migration Plan

1. Implement `src/services/bp_terminal.h` and `src/services/bp_terminal.c`.
2. Update `_putchar` in `src/printf-4.0.0/printf.c` to use `bp_terminal`.
3. Hook `bp_term_service()` into `bp_service(0)` in `src/kernel/bp_kernel.c`.
4. Implement host unit tests `tests/test_terminal_service.c` and `tests/test_terminal_service.sh`.
5. Verify zero dynamic heap usage and build target `bus_pirate6.uf2`.
