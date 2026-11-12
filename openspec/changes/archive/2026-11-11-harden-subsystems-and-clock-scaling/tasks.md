# Tasks

## 1. PS/2 Sniffer Hardening
- [x] 1.1 In `src/mode/ps2.c`, fix `kbd_sm` on `pio1` to `kbd_h2d_sm` in `pio_sm_set_consecutive_pindirs(pio1, kbd_h2d_sm, DAT_GPIO, 2, false)`
- [x] 1.2 Remove duplicate `pio_sm_claim()` calls on lines 1076–1079 in `src/mode/ps2.c` and verify leak-free start/stop cycles

## 2. Infrared AIR Packet Bounds Hardening
- [x] 2.1 In `src/commands/infrared/irtxrx.c`, add bounds checking to `irtx_transmit()` to guard delimiter scanning against buffer overruns
- [x] 2.2 Add pre-condition check `if (datacnt >= count_of(data))` before setting `data[datacnt]` on both mark and space iterations

## 3. Clock-Independent PWM Frequency Scaling
- [x] 3.1 In `src/commands/global/pwm.c`, dynamically scale PWM period and duty calculations using `clock_get_hz(clk_sys)` instead of 125 MHz constant
- [x] 3.2 Verify PWM output frequencies match requested settings at 150 MHz default and overclocked frequencies

## 4. PSU Auto-Recovery Policy
- [x] 4.1 In `src/pirate/psu.c`, add transient retry logic in `psu_enable()` with bounded backoff for high-capacitance loads
- [x] 4.2 Verify with host unit tests and live hardware test
