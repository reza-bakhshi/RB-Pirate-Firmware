# Design: Fix RFID Port Parity and Tag Lifecycle

## Architecture Overview

The RFID subsystem connects the RP2350 microcontroller to an NXP PN532 NFC reader module using High-Speed UART (HSU).
The Bus Pirate supports two pin pairs for the PN532 connection:
1. `pin_pair 0`: TX = `BIO4` (GP12), RX = `BIO5` (GP13) $\rightarrow$ PL011 `uart0`
2. `pin_pair 1`: TX = `BIO0` (GP8),  RX = `BIO1` (GP9)  $\rightarrow$ PL011 `uart1`

### 1. Hardware Peripheral Arbitration and Clean Teardown

To avoid silent bus contention or breaking debug/terminal sessions:
- Hardware peripheral indices `BP_RES_PERIPH_UART0` (0) and `BP_RES_PERIPH_UART1` (1) are declared under `BP_RES_CLASS_PERIPH`.
- `pn532_init(tx, rx, baud)` queries `bp_res_acquire(BP_RES_CLASS_PERIPH, periph_id, BP_RES_OWNER_MODE_BASE, "pn532_uart")`. If another subsystem (such as debug UART) holds the peripheral lease, `pn532_init` fails cleanly.
- `rfid_setup_exc()` verifies both `system_bio_is_debug()` and the return value of `pn532_init()` before updating pin labels (`system_bio_update_purpose_and_label()`).
- On `pn532_deinit()`:
  - If the module was connected and ready, sends a `PN532_CMD_POWERDOWN` command.
  - Calls `uart_deinit(pn532_uart)`.
  - Releases the peripheral lease via `bp_res_release(BP_RES_CLASS_PERIPH, periph_id, BP_RES_OWNER_MODE_BASE)`.
  - Restores both TX and RX BIO pins to GPIO function `GPIO_FUNC_SIO` and sets their directions to input using `bio_input(pin)`.

### 2. Unsupported Tag Handling and File Hygiene

The PN532 can detect multiple tag families (MIFARE Classic, MIFARE Ultralight/NTAG, MIFARE Plus, ISO14443-4 DESFire, Topaz/Jewel, FeliCa). However, memory dump commands (`dump -f` / `dump -b`) are only implemented for MIFARE Classic and MIFARE Ultralight/NTAG.
- When an unsupported tag (such as SAK 0x20 DESFire) is presented to `dump_handler`:
  - Output an explicit message: `Direct dump is not supported for this tag type (SAK: 0x%02X).`
  - If a file was opened for writing (`fil_open`), close it with `f_close(&fil)` and immediately call `f_unlink(file_name)` so no truncated or empty file persists on flash storage.
  - Exit immediately without printing `Dump saved successfully`.
- In `read_handler`, if the tag is not Classic and not Ultralight/NTAG, print `Memory preview not supported for this tag type (SAK: 0x%02X).`

### 3. Cooperative Cancellation & Responsiveness

In the kernel architecture, commands must observe cooperative cancellation (`bp_job_check_cancelled()`) and user abort keypresses (`rx_fifo_try_get(&c)`):
- `pn532_uart_read_byte()` polls `uart_is_readable()` in a tight loop with a timeout. Checking `bp_job_check_cancelled()` inside this loop allows all long PN532 command sequences to abort in $< 10\text{ ms}$ upon job cancellation.
- In `forum4_handler`, check `bp_job_check_cancelled()` in the main APDU emulation loop.
- In `clone_handler`, check `bp_job_check_cancelled()` while waiting for source card removal and target card placement.
- In `erase_handler`, check `bp_job_check_cancelled()` in the interactive `Continue? (y/n):` prompt.

### 4. Unit Testing and Mock Harness

Host unit tests in `tests/test_rfid.c` verify:
- PN532 HSU packet framing: Preamble (`0x00`), Start codes (`0x00 0xFF`), Length, Length Checksum (`~len + 1`), Direction identifier (`0xD4` / `0xD5`), Data, Data Checksum (`~sum + 1`), and Postamble (`0x00`).
- ACK packet detection (`00 00 FF 00 FF 00`).
- Tag classification from ATQA and SAK fields (MIFARE Classic 1K, 4K, Mini, Ultralight, NTAG213, NTAG215, NTAG216, MIFARE Plus, DESFire).
- Status code string mappings.
