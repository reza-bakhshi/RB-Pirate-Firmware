# Glitch, display, storage and SWD hardening

## Delivered changes

| Finding | Change | Verification |
|---|---|---|
| O06/O07, unsafe glitch implementation | Reject power glitch requests; never drive PSU current-limit override. Logic glitches require HiZ mode, unassigned pins, an idle job engine and a resource lease. Dispatch through cooperative Core 1 slices; bounded timing/count, edge selection, cancellation, per-edge timeout, and HiZ cleanup. | Invalid ranges and malformed options, valid multi-pulse execution, cancellation, mode refusal and released lease assertions in CAN/glitch suite. Physical waveform accuracy remains unverified. |
| O04/O05, unsupported HSTX output | Remove incomplete register/DMA setup. RB-Pirate reports unsupported without touching hardware; host renderer remains testable. | Dedicated unsupported-board test plus existing renderer tests. This prevents unsupported output; it does not implement DVI. |
| O08, missing WAIT retries | Retry WAIT according to configured budget, capped additionally at 100 ms; return FAULT to host. | Injected ACK sequence tests cover WAIT-to-success, exhausted WAIT and FAULT. Physical SWD parity, turnaround and USB timing still require hardware. |
| O09, sector arithmetic | Reject null buffers, zero or >128-sector requests, overflowing sector ranges and byte-address conversion overflow before touching SPI. | Direct invalid read/write parameter assertions plus existing DMA tests. Card capacity metadata validation remains open. |
| O10, LCD completion | Bounded status-returning idle waits; timeout retains DMA ownership of source buffer. Reject misaligned/oversized pixel transfers and avoid changing windows during a transfer. | Active timeout retains busy state; idle success, oversize rejection and completion assertions in DMA suite. SPI/panel wiring and completion of the SPI shifter remain unverified. |
| O11, calibration overwrite | Write/sync/close temp, retain backup during replacement, recover missing primary from backup, preserve sole backup after interrupted save. | Injected sync/rename errors preserve previous calibration; missing primary loads backup. This is a recovery strategy, not a FAT power-loss atomicity guarantee. |

No runtime heap allocation was introduced. Resource IDs use an existing spare peripheral registry slot, so the lease table does not grow. The pin purpose registry gates BIO availability while the synchronous control-to-worker call suppresses parsing another command.

## Board limits

`src/platform/rb-pirate.h` assigns GPIO16..19 to PSU reset/fuse/override/enable. It declares no dedicated crowbar output or DVI pin mapping. A question requesting external adapter/alternate-board wiring remains pending. Enabling voltage glitching or DVI without that information would misrepresent board capabilities. No hardware was flashed or driven during these checks.

Physical AMUX DMA sequencing, HSTX frame generation and actual RP2040 support remain unfinished. AMUX uses the bounded synchronous implementation from the previous batch. Correct autonomous acquisition needs a mux-settling schedule, ADC FIFO/DREQ integration, ownership for manual reads and verified Core 0 publication; leasing channels alone is insufficient. A full physical-video implementation needs board wiring and a separate tested timing backend.

Calibration recovery does not yet retry a backup after a present primary fails numeric validation. The saved JSON still has no persisted authenticity/checksum field. The LCD driver still requires a complete panel/shared-SPI integration review. These limits are retained explicitly rather than treating a passing host suite as hardware certification.

## Validation

- Normal unified suite: 36 passed, 2 hardware skips, zero failures (38 suites).
- Both requested incremental firmware builds passed without warning/error diagnostics after correcting the infrared bytecode forward declaration. Both build directories still compile RP2350.
- RP2350 memory guard passed at 388,504 bytes static main RAM, 135,784 bytes remaining, 616 bytes below the static ceiling; no increase from the earlier batch.
- CRLF-aware whitespace check passed.
- Full unified run with GCC/G++ forced to `-O2 -fsanitize=address,undefined`: **36 passed, 2 hardware skips, zero failures**. Local logs: `/tmp/bp-safe-*.log`.
