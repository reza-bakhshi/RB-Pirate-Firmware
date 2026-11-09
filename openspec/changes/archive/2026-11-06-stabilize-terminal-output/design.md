# Design

## Context

See `proposal.md` — Why. The current output path is:

```
printf → _putchar → tx_fifo_put → spsc_queue_add_blocking
```

`tx_fifo_put()` spins until the 1 KB `tx_fifo` has space. `tx_fifo_wait_drain()` also spins with no
timeout. Core 1's `tx_fifo_service()` returns early when `tud_cdc_n_write_available(0) < 64`, before
the disconnect/flush path, so a connected host that stops reading can leave the ring full forever.

This change only fixes the terminal output path. It does not move terminal ownership to a different
core; that is a later roadmap change (`route-terminal-io-through-control-core`).

## Goals / Non-Goals

**Goals:**

- No command path can block indefinitely on terminal output.
- A paused or disconnected host cannot leave the terminal ring permanently full.
- The firmware reports truncation instead of silently losing data or flooding.
- Terminal health is observable through the existing `health` command.
- Existing callers keep working with minimal API churn.

**Non-Goals:**

- Atomic whole-line or whole-VT100-sequence writes.
- Moving terminal ownership to Core 0.
- Binary-mode (CDC1) transport changes.
- Changing command syntax or output formatting.

## Decisions

### 1. Non-blocking producer API

`tx_fifo_put()` becomes non-blocking and returns a status:

```c
typedef enum {
    BP_TX_OK = 0,
    BP_TX_DROPPED,
    BP_TX_TIMEOUT,
} bp_tx_status_t;

bp_tx_status_t tx_fifo_put(char c);
bp_tx_status_t tx_fifo_write(const char *buf, uint32_t len);
```

`printf`'s `_putchar()` uses `tx_fifo_put()` and ignores the status, but the drop counters are
updated. Callers that care (for example `txtest`) use the status.

**Alternative considered:** keep `tx_fifo_put()` blocking but add a timeout. Rejected because every
existing caller would need to handle a new error path, and a timeout still stalls the UI for the
whole timeout duration. Dropping output is the correct behavior for a diagnostic terminal.

### 2. Byte-level drop with one truncation marker

When the ring is full, drop the new byte and increment `tx_dropped`. Set `tx_overflow_pending` when
the first byte is dropped in an overflow episode. Once the ring level falls below a low-water mark,
the drain path emits one marker:

```
[output truncated: N bytes]
```

The marker is written directly into the ring only when there is enough space; otherwise it remains
pending. The marker itself is never counted recursively.

**Alternative considered:** block the producer for a short grace period. Rejected — it still stalls
command execution and does not solve a permanently paused host.

**Accepted limitation:** dropping bytes can split a VT100 escape sequence and leave the terminal in
an odd color/scroll state. The marker tells the user output was lost. Atomic record writes are a
follow-up if this becomes a practical problem.

### 3. Bounded drain with a compatibility wrapper

```c
bool tx_fifo_wait_drain_timeout(uint32_t timeout_ms);
void tx_fifo_wait_drain(void);  // bounded wrapper, default 1000 ms
```

Uses `absolute_time_t` and `tight_loop_contents()`. Returns `false` on timeout. The old
`tx_fifo_wait_drain()` symbol is kept as a bounded wrapper because it has ~80 call sites; changing
the signature everywhere would turn a safety fix into a mass refactor. Critical callers (`txtest`)
use the timeout variant and report a timeout.

### 4. Correct Core 1 drain ordering

`tx_fifo_service()` is reordered:

1. If CDC0 is not connected, flush the terminal ring and return.
2. If CDC0 is connected but the TinyUSB write FIFO has no space, return without consuming (the ring
   stays full, but the producer no longer blocks).
3. Otherwise, move up to 64 bytes from the ring to TinyUSB and flush.
4. Toolbar/status-bar transmission remains gated on an empty terminal ring.

The key change is that the producer never waits for step 2.

### 5. Health counters

Add a small `bp_terminal_health_t`:

```c
typedef struct {
    uint32_t dropped_bytes;
    uint32_t overflow_events;
    uint32_t high_water;
    uint32_t drain_timeouts;
} bp_terminal_health_t;
```

`health` prints these values. `high_water` is updated by the producer. `drain_timeouts` is updated by
`tx_fifo_wait_drain_timeout()`.

The command must be added as a `bp_command_def` and registered in the System category of
`src/commands.c`; there is no existing `health` command in the tree.

### 6. Compatibility

Keep `tx_fifo_put()` and `tx_fifo_write()` names so existing call sites compile. Change the return
type from `void` to `bp_tx_status_t`; C allows ignoring a return value. Add a `tx_fifo_try_put()` alias
for callers that want the old non-blocking behavior explicitly.

## Risks / Trade-offs

- **Risk:** output is lost during heavy dumps. → Mitigation: counter plus one marker; the device
  stays responsive, which is the priority.
- **Risk:** a split VT100 sequence corrupts terminal state. → Mitigation: marker; document the
  limitation; follow-up atomic record writes if needed.
- **Risk:** `printf` callers silently ignore the status. → Mitigation: counters and `health`; the
  command layer can adopt status-aware output later.
- **Risk:** power loss during a long dump looks like truncation. → Mitigation: marker is explicit and
  counted; no silent loss.

## Migration Plan

1. Add counters and the non-blocking API.
2. Migrate `printf` and `txtest`.
3. Reorder `tx_fifo_service()`.
4. Add `health` output.
5. Run host tests and the paused-host hardware test.
6. Archive this change; the next change (`fix-cross-core-io-violations`) builds on the counters.

Rollback: revert the change. No persistent data format changes.

## Open Questions

None. The atomic-record question is deliberately deferred and does not change the requirements or
the task breakdown.
