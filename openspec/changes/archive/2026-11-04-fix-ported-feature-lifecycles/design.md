# Design

## Context

See `proposal.md` — Why. The static audit uses the pinned sources in `FEATURES.md`. The defects below
were confirmed by reading the current code, not by hardware observation.

## Goals / Non-Goals

**Goals:**

- Make mode entry/exit resource-safe.
- Remove the shared S1 defects before feature parity work.
- Keep protocol behavior and user commands unchanged.

**Non-Goals:**

- Full resource-lease manager (B3).
- Hardware parity fixes for individual protocols (F3-F9).
- Rewriting the PIO drivers.

## Decisions

### 1. IR PIO lifecycle

`irio_pio_tx_init()` and `irio_pio_rx_init()` gain `tx_initialized` / `rx_initialized` flags.
Deinit returns immediately when the corresponding init never ran. The RX deinit removes
`pio_config_rx_mod_freq` as well as the mark/space programs. All state machines are disabled and
their FIFOs cleared before program removal.

### 2. LCDTAP SPI3 teardown

The SPI3 path stores `spi_3line_mode0_program` and its offset. A dedicated `spi3Deinit()` disables
the SM, clears FIFOs, removes the program and unclaims the SM. SPI4 continues to use
`spiSlaveDeinit()`.

### 3. USB clock and PSU

Save `clock_get_hz(clk_sys)` before `set_sys_clock_khz(120000, true)`. Restore it in
`usb_cleanup()` with `set_sys_clock_khz(saved_khz, true)`. Replace direct `psu_enable`/`psu_disable`
calls with `psucmd_enable`/`psucmd_disable`.

### 4. Global PIO teardown

`pio_destroy()` is removed from USB and PS2. Each path records the SMs and program offsets it
claims and releases only those. If the existing code is too entangled for a safe one-pass change,
the first step replaces the global clear with a tracked release and keeps the external behavior
identical.

### 5. RF24 continuous carrier

`nrf24_jam_channel()` toggles CE low, writes `RF_CH`, waits 5 us, then sets CE high. This ensures the
PLL retunes for each channel. The `jam -g wifi` output and help call it a **WiFi-band carrier
sweep**; it does not claim 802.11 injection.

### 6. Logic analyzer allocation

`logicanalyzer_setup()` checks `mem_alloc()` and returns false. `logic_analyzer_configure()` refuses
to run when `la_buf == NULL`.

### 7. ADC lock

`adc_busy_wait(false)` releases `busy` inside the spinlock. `busy` is `volatile`. Acquisition and
release remain paired.

### 8. Debug pin guard

Add a small helper that checks `system_config.pin_func[pin] == BP_PIN_DEBUG`. RFID checks its chosen
TX/RX pair before `pn532_init()`. The same helper can be used by later feature fixes.

## Risks / Trade-offs

- **Risk: USB/PS2 PIO refactor is invasive.** → Mitigation: keep the same programs and timing,
  change only ownership/release; build and run host tests.
- **Risk: CE toggling changes jam timing.** → Mitigation: keep the 500 us dwell; add a comment and
  verify on hardware later.
- **Risk: clock restore fails.** → Mitigation: log the requested and actual frequency and keep the
  previous value if restore fails.

## Migration Plan

1. IR and LCDTAP lifecycle.
2. USB clock/PSU and RF24 jam.
3. PS2/USB PIO ownership.
4. Logic analyzer and ADC.
5. Debug-pin guard.
6. Build, host tests, static checks, archive.
