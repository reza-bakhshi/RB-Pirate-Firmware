# Tasks

## 1. Cellular AT Engine & Result Code Hardening

- [x] 1.1 Add cooperative cancellation check `cell_check_cancel()` and immediate SMS prompt (`> `) detection to `cell_cmd()` in `src/commands/cell/cell_at.c`.
- [x] 1.2 Harden `last_line_is_final()` and `cell_cmd_ok()` to correctly detect all final result codes (`OK`, `ERROR`, `+CME ERROR`, `+CMS ERROR`, `NO CARRIER`, `BUSY`, `NO ANSWER`, `NO DIALTONE`) in `src/commands/cell/cell_at.c`.
- [x] 1.3 Add `ATE0` echo suppression to `cell_modem_detect()` and strip echo lines in `cell_first_line()` in `src/commands/cell/cell_at.c`.

## 2. Command Handlers, SMS Submission & URC Handling

- [x] 2.1 Fix `sms_send_text()` in `src/commands/cell/cell.c` to wait for actual submission result (`+CMGS` / `OK` / error) with cancellation checks, and validate recipient numbers.
- [x] 2.2 Implement asynchronous USSD URC wait loop with cancellation checks in `cell_ussd_handler()` in `src/commands/cell/cell.c`.
- [x] 2.3 Wire hardware peripheral lease arbitration for `BP_RES_PERIPH_UART0` under `BP_RES_OWNER_MODE_BASE + HWUART` in `src/mode/hwuart.c`.

## 3. Host Tests & Verification

- [x] 3.1 Create host unit test suite `tests/test_cell.c` and `tests/test_cell.sh` verifying AT parser, final codes, SMS prompt/submission, USSD URC, buffer safety over 100 iterations, and cancellation responsiveness.
- [x] 3.2 Run test suite under AddressSanitizer and UBSan (`tests/test_cell.sh`).
- [x] 3.3 Build `bus_pirate6` firmware target on RP2350 and verify static RAM consumption <= 380 KB.
