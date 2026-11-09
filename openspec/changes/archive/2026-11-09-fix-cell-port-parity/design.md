# Design: fix-cell-port-parity

## Context

See `proposal.md` for the motivation and `specs/cell/spec.md` for requirements. The cellular modem subsystem executes on Core 0 (CTRL), interacting directly with the hardware UART0 peripheral (`M_UART_PORT`) configured in `HWUART` mode. Long-running AT operations (such as 60-second operator scans or network SMS submissions) previously blocked cooperatively, and asynchronous network URCs (such as USSD balance responses) were dropped due to premature termination upon receiving the initial modem `OK`.

## Goals / Non-Goals

**Goals:**
- Provide sub-100ms cooperative cancellation across all cellular AT command and submission loops via `rx_fifo_try_get()` and `bp_job_check_cancelled()`.
- Harden AT command response parsing: recognize telephony termination result codes (`NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE`), fix `cell_cmd_ok()` to inspect only the trailing line, and detect SMS `> ` prompts immediately.
- Fix SMS submission timeout logic to wait up to 30s for the network ACK instead of aborting after 1s of silence.
- Capture asynchronous USSD `+CUSD:` URC responses when returned after the initial command `OK`.
- Suppress modem local echo with `ATE0` during detection and make `cell_first_line()` resilient against leading echo lines.
- Enforce kernel lease tracking on `BP_RES_PERIPH_UART0` across UART mode setup and cleanup.
- Maintain static memory model: zero heap allocations, static 1600-byte response buffer, fixed stack limits.

**Non-Goals:**
- Supporting non-standard proprietary cellular AT modems (only standard 3GPP TS 27.007 / 27.005 AT command set is supported).
- Background multiplexed PPP / packet data daemon (out of scope for Bus Pirate shell).
- Upstream hardware modems not using 3.3V UART levels.

## Decisions

### 1. Cooperative Cancellation via unified polling
- **Choice**: Check `cell_check_cancel()` inside every byte-receive and idle wait loop in `cell_cmd()`, `sms_send_text()`, and `cell_ussd_handler()`.
- **Implementation**:
  ```c
  static inline bool cell_check_cancel(void) {
      char c;
      if (rx_fifo_try_get(&c)) return true;
      return bp_job_check_cancelled();
  }
  ```
- **Rationale**: Immediate response to user input (any key or Ctrl+C) or job manager cancellation signals, preventing multi-second hangs during missing or unresponsive modems.
- **Alternatives considered**: Relying solely on hardware UART timeouts; rejected because AT timeouts reach up to 65 seconds for network scans.

### 2. Precise Last-Line Result Code Parsing
- **Choice**: Use `last_line_is_final()` exclusively for finality and success checks in `cell_cmd_ok()`.
- **Implementation**:
  Expand recognized final codes:
  - `OK` -> final, success
  - `ERROR` -> final, error
  - `+CME ERROR...` -> final, error
  - `+CMS ERROR...` -> final, error
  - `NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE` -> final, error
- **Rationale**: Searching with `strstr(resp, "OK")` causes false negatives when responses contain words like `"BOOK"` or false positives on echoing commands. Evaluating strictly the last CRLF-delimited line guarantees parser fidelity.

### 3. Immediate SMS Prompt Detection & Reliable Submission
- **Choice**: Inspect the trailing response in `cell_cmd()` for `> ` (`len >= 2 && response[len-2] == '>' && response[len-1] == ' '`). When detected, return immediately without waiting for `CELL_IDLE_GAP` (750ms).
- **Submission Loop**: After submitting text terminated with `0x1A` (Ctrl+Z), wait for the actual result code (`+CMGS` / `OK` / error) with a 30s timeout and cooperative cancellation. Do not abort when silence occurs before the network responds.

### 4. Asynchronous USSD URC Handling
- **Choice**: If `AT+CUSD=1,...` returns `OK` but lacks an inline `+CUSD:` payload, poll UART RX for up to 10s for the network URC line, checking for cancellation on each iteration. Print the URC as soon as it arrives.

### 5. Echo Suppression and First-Line Extraction
- **Choice**: Execute `ATE0` in `cell_modem_detect()` upon initial probe. In `cell_first_line()`, check whether the first line starts with `AT` or `at`; if so, advance past the echo line to extract the true response payload.

### 6. UART Peripheral Lease Tracking
- **Choice**: Acquire `BP_RES_PERIPH_UART0` under `BP_RES_OWNER_MODE_BASE + system_config.mode` in `hwuart_setup_exc()`. Release it in `hwuart_cleanup()`.
- **Rationale**: Prevents peripheral collisions if another subsystem (e.g. PN532 or debug UART) has claimed UART0.

## Risks / Trade-offs

- **[Risk] User keystrokes intended for shell may abort a running AT command**
  - *Mitigation*: The shell prompt is inactive while a command executes. Draining the single keypress that cancelled the command prevents spurious shell input after returning.
- **[Risk] Modems with slow network registration delay USSD reply beyond 10s**
  - *Mitigation*: 10 seconds matches standard cellular network USSD timeouts; if cancelled or timed out, the prompt returns cleanly without locking the terminal.
- **[Risk] Long responses (> 1600 bytes) truncated**
  - *Mitigation*: Buffer boundary clamping (`CELL_RESPONSE_BUFFER - 1`) prevents memory overrun, and `last_line_is_final()` operates correctly on truncated stream buffers.
