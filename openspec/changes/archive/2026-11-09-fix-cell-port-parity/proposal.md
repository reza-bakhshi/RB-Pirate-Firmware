# Proposal: fix-cell-port-parity

## Why

The cellular AT modem feature (`cell*` commands in UART mode) ported from ESP32-Bit-Pirate lacks cooperative cancellation during multi-second modem operations (such as 60-second operator scans or SMS submissions), contains a timing bug in SMS send loop where silence > 1s was prematurely treated as successful submission before the modem could reply with `+CMGS`, drops asynchronous USSD responses (`+CUSD:`) because the AT engine returned on `OK` before the network answer arrived, misses call failure result codes (`NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE`), and fails to arbitrate the hardware UART0 peripheral lease (`BP_RES_PERIPH_UART0`) with the kernel resource manager.

This change establishes responsive cancellation (< 100ms), hardens AT result code and URC parsing, fixes SMS submission and USSD reception, integrates UART0 resource lease tracking, and adds host regression tests.

## What Changes

- **Cooperative cancellation in AT engine**: Add non-blocking cooperative cancellation checks (`rx_fifo_try_get()` and `bp_job_check_cancelled()`) to `cell_cmd()`, `sms_send_text()`, and command loops, allowing user keypress or Ctrl+C to abort multi-second operations immediately.
- **Harden AT result parsing and call status**: Extend `last_line_is_final()` to detect telephony termination codes (`NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE`). Fix `cell_cmd_ok()` to evaluate the true last line instead of searching for arbitrary substring `"OK"`.
- **Immediate SMS prompt & reliable submission**: Recognize the SMS prompt (`> `) immediately in `cell_cmd()` without idling. Fix `sms_send_text()` to wait for the actual `+CMGS` / `OK` / `+CMS ERROR` response instead of exiting after 1s of transmission silence, and validate destination phone number characters.
- **USSD URC handling & echo suppression**: In `cellussd`, when `AT+CUSD` yields immediate `OK`, await the asynchronous `+CUSD:` URC with cancellation support. Send `ATE0` in `cell_modem_detect()` to disable echo, and harden `cell_first_line()` against echoed command headers.
- **Peripheral lease arbitration**: Enforce `BP_RES_PERIPH_UART0` acquisition and release under `BP_RES_OWNER_MODE_BASE + HWUART` during UART mode setup and teardown.
- **Host unit test suite**: Add `tests/test_cell.c` and `tests/test_cell.sh` verifying 100 consecutive AT commands, buffer boundary security, URC handling, cancellation responsiveness, and result parsing under AddressSanitizer and UBSan.

## Capabilities

### Modified Capabilities

- `cell`: Strengthen failure handling with cooperative cancellation, harden AT result parsing, SMS submission, USSD URC handling, and UART peripheral lease arbitration.

## Impact

- `src/commands/cell/cell.c`: Add cooperative cancellation, fix SMS send loop, handle asynchronous USSD URC, validate phone numbers.
- `src/commands/cell/cell_at.c`: Add cancellation to `cell_cmd()`, recognize SMS prompt `> `, extend final result codes, fix `cell_cmd_ok()`, suppress echo in `cell_modem_detect()`, ignore echo in `cell_first_line()`.
- `src/commands/cell/cell.h`: Expose cancellation state query helper if needed.
- `src/mode/hwuart.c`: Acquire `BP_RES_PERIPH_UART0` in `hwuart_setup_exc()` and release in `hwuart_cleanup()`.
- `tests/test_cell.c`, `tests/test_cell.sh`: Host test suite for cellular parser, URCs, cancellation, and buffer bounds.
