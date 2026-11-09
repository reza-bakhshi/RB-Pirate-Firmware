# Change: Fix RFID Port Parity and Tag Lifecycle (F5)

## Why

The Bus Pirate RFID mode interfaces with an NXP PN532 NFC/RFID controller over High-Speed UART (HSU). While basic framing and commands are implemented, reaching full feature parity and stability requires resolving several hardware arbitration, tag lifecycle, and cancellation gaps:
1. **Peripheral arbitration & pinmux teardown**: PN532 HSU operates over either BIO0/BIO1 (`uart1`) or BIO4/BIO5 (`uart0`). The driver must arbitrate against active debug/terminal UART instances using `system_bio_is_debug()` and kernel resource tracking (`bp_res_acquire(BP_RES_CLASS_PERIPH, ...)`). On deinitialization (`pn532_deinit()`), the UART peripheral must be released and BIO pins cleanly restored to `GPIO_FUNC_SIO` with input buffer directions.
2. **Unsupported tag error handling**: In `dump_handler`, encountering an unsupported tag (such as ISO14443-4 DESFire or generic ISO14443A) printed an error but subsequently closed the output file and announced "Dump saved successfully", leaving an empty or truncated file on storage. Unsupported operations must close and unlink (`f_unlink()`) any created file and report the unsupported status without claiming success.
3. **Non-blocking cooperative cancellation**: Several interactive and polling loops (such as the APDU loop in `forum4_handler`, the card-swap wait in `clone_handler`, the confirmation prompt in `erase_handler`, and the byte-read timeout loop in `pn532_uart_read_byte`) either did not check `bp_job_check_cancelled()` or hung waiting for keyboard input. Every wait must break cleanly in under 100 ms upon cancellation.
4. **Host unit test coverage**: There is currently no dedicated host unit test suite for PN532 HSU framing, checksum calculation, ACK validation, tag identification, or status formatting.

## What Changes

1. **Hardware Arbitration and Pinmux Restoration (`src/pirate/pn532.c`, `src/kernel/bp_resource.h`, `src/debug_uart.c`)**:
   - Define peripheral indices `BP_RES_PERIPH_UART0` and `BP_RES_PERIPH_UART1` in `bp_resource.h`.
   - Acquire peripheral lease in `pn532_init()` via `bp_res_acquire(BP_RES_CLASS_PERIPH, ...)`.
   - In `pn532_deinit()`, release peripheral lease and restore TX/RX pins via `bio_set_function(..., GPIO_FUNC_SIO)` and `bio_input(...)`.
   - Ensure `rfid_setup_exc()` checks `pn532_init()` success before claiming pin purpose and labels.
2. **Unsupported Tag Handling and File Cleanup (`src/mode/rfid.c`)**:
   - In `dump_handler()`, if a tag is unsupported (non-Classic, non-Ultralight/NTAG), cleanly close `fil`, invoke `f_unlink(file_name)` to remove any empty or partial dump file, and exit without printing success.
   - In `read_handler()`, output an explicit unsupported memory preview message when tag is neither Classic nor Ultralight/NTAG.
3. **Cooperative Cancellation Hardening (`src/pirate/pn532.c`, `src/mode/rfid.c`)**:
   - In `pn532_uart_read_byte()`, check `bp_job_check_cancelled()` inside the wait loop to abort promptly when a command is interrupted.
   - In `forum4_handler()`, check `bp_job_check_cancelled()` in the emulation loop.
   - In `clone_handler()`, handle cancellation during the source removal / card placement wait.
   - In `erase_handler()`, verify `bp_job_check_cancelled()` during the `Continue? (y/n):` confirmation loop.
4. **Host Unit Test Suite (`tests/test_rfid.c`, `tests/test_rfid.sh`)**:
   - Implement unit tests covering PN532 frame assembly, length checksum (LCS), data checksum (DCS), ACK pattern detection, response parsing, and ATQA/SAK tag type identification.
   - Run tests under ASan and UBSan.
5. **Firmware Target and Full Regression**:
   - Compile RP2350 target firmware (`bus_pirate6.uf2`) and verify static RAM ceiling $\le 380\text{ KB}$ and 4 KB fixed stacks.
   - Run full regression across all `tests/test_*.sh` suites.

## Impact

- Safe and collision-free UART pinmux arbitration with terminal/debug UART.
- Zero leftover partial dump files upon encountering unsupported tags.
- Responsive cooperative cancellation across all RFID commands.
- Verified test suite and firmware build with zero regressions.
