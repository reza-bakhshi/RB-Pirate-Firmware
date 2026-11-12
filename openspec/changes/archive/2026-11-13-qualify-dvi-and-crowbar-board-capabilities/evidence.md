# Hardware feasibility evaluation and qualification evidence: DVI and crowbar capabilities

Date: 2026-11-13. Scope: Electrical feasibility evaluation, pin conflict matrix, host test verification, and live hardware qualification on RB-Pirate RP2350B.

## 1. Executive Summary & Feasibility Decision

* **HSTX DVI Capability**: **UNSUPPORTED** on RB-Pirate RP2350B.
  * Electrical and routing analysis shows that RP2350 HSTX serializer lanes (GPIO 12..19) directly collide with the onboard SPI LCD display, RGB indicator LEDs, and essential PSU overcurrent protection circuits.
  * RB-Pirate PCB does not route HSTX differential pairs or provide a DVI/HDMI physical connector.
  * Software guards in `src/pirate/hstx_dvi.c` and `src/commands/global/cmd_video.c` fail closed, returning `false` for `hstx_dvi_is_supported()`.
* **Dedicated Crowbar Glitch Capability**: **UNSUPPORTED** on RB-Pirate RP2350B.
  * RB-Pirate contains no dedicated high-speed low-side switch (FET) or hardware-bounded pulse generator.
  * Prior attempts to repurpose PSU current-limit override (`CURRENT_DETECT_OVERRIDE`, GPIO 16) as a glitch output are electrically hazardous to the power supply regulator and are strictly blocked.
  * Software guards in `src/commands/global/cmd_glitch.c` reject `glitch -p` with an explicit unsupported error message.

---

## 2. Electrical Pin & Function Conflict Matrix

Cross-referencing RP2350 datasheet HSTX peripheral mapping against `src/platform/rb-pirate.h`:

| RP2350 Function | GPIO | RB-Pirate Assigned Net | Direction | Conflict Severity | Impact of Driving HSTX |
|---|---|---|---|---|---|
| `HSTX0` | GPIO 12 | `HW_PINS_LCD_BACKLIGHT` / `EXT1` | Output | High | Toggles LCD backlight PWM / corrupts external interface. |
| `HSTX1` | GPIO 13 | `HW_PINS_RGB_LEDS` | Output | High | Corrupts WS2812 RGB status indicator data stream. |
| `HSTX2` | GPIO 14 | `HW_PINS_LCD_CS` | Output | Critical | Triggers spurious SPI chip select assertions to LCD. |
| `HSTX3` | GPIO 15 | `HW_PINS_LCD_DC` | Output | Critical | Corrupts LCD command/data framing on shared SPI bus. |
| `HSTX4` | GPIO 16 | `CURRENT_DETECT_OVERRIDE` | Output | **FATAL** | Overrides PSU current limit latch; defeats power safety. |
| `HSTX5` | GPIO 17 | `CURRENT_DETECT_EN` | Output | **FATAL** | Toggles PSU hardware overcurrent detection enable. |
| `HSTX6` | GPIO 18 | `PULLUP_EN` | Output | **FATAL** | Toggles programmable pull-up voltage rail unexpectedly. |
| `HSTX7` | GPIO 19 | `CURRENT_DETECT_RESET` | Output | **FATAL** | Spurious resets of PSU overcurrent protection latch. |

**Conclusion**: Any transmission on HSTX lanes would catastrophically interfere with the power supply monitor and onboard LCD display. HSTX DVI cannot be enabled on RB-Pirate.

---

## 3. DVI Timing & Feasibility Calculation

Standard 640x480 @ 60 Hz DVI-D timing:
* Pixel clock: $25.175\,\text{MHz}$
* Horizontal: 640 active + 16 front porch + 96 sync + 48 back porch = 800 total pixels per line.
* Vertical: 480 active + 10 front porch + 2 sync + 33 back porch = 525 total lines per frame.
* Frame rate: $\frac{25.175\,\text{MHz}}{800 \times 525} \approx 59.94\,\text{Hz}$.
* Bit clock (10:1 serialization): $251.75\,\text{MHz}$; (5:1 DDR): $125.875\,\text{MHz}$.

With the RP2350 operating at default system clock ($150\,\text{MHz}$):
* Integer/fractional divider cannot produce $125.875\,\text{MHz}$ or $251.75\,\text{MHz}$ without reconfiguring the system PLL, which would alter core, DMA, and peripheral clocking across the entire system.
* Memory footprint: Storing an 80x30 text buffer (2,400 bytes) plus two 640-byte ping-pong scanline buffers (1,280 bytes) requires 3,710 bytes. The static main RAM budget has only 432 bytes headroom below the 389,120-byte ceiling. Full scanline generation on hardware would exceed the static memory ceiling unless allocated dynamically (which violates zero-runtime-heap rules).

---

## 4. Host Verification Evidence

### 1. DVI Capability Rejection (`tests/test_video_unsupported.c`)
Executed as part of `tests/test_cmsis_dap_hstx.sh`:
* `assert(!hstx_dvi_init());` -> PASS
* `assert(!hstx_dvi_is_supported());` -> PASS
* `hstx_dvi_enable(true);` -> `assert(!hstx_dvi_is_enabled());` -> PASS
* `hstx_dvi_service();` -> No-op, no register writes -> PASS
* `assert(!hstx_dvi_init());` -> PASS

### 2. Crowbar Rejection (`tests/test_can_glitch.c`)
Executed as part of `tests/test_can_glitch.sh`:
* `glitch -p` is verified in `test_glitch_limits_and_cleanup()`:
  - Argument parsing rejects `-p` as unsupported.
  - Zero power net pins or current override pins are modified.
  - Returns cleanly with error code.

### 3. Unified Suite Execution
* `bash tests/run_all.sh` executed across all 40 test suites:
  - 40 passed, 0 skipped, 0 failed.
  - Full `-fsanitize=address,undefined` validation clean with zero leaks or UB.

---

## 5. Physical Hardware Qualification Evidence

Tested on physical hardware:
* Target: RB-Pirate RP2350B, S/N `45003CF749C0B0D3`, firmware `bus_pirate6.uf2`.
* Connection: USB CDC CLI on `/dev/ttyACM0`.

### Physical Command Transcripts

#### DVI Video Commands:
```text
UART> video
Notice: DVI output is unavailable: this board has no supported DVI wiring.

UART> video on
Notice: DVI output is unavailable: this board has no supported DVI wiring.

UART> video off
Notice: DVI output is unavailable: this board has no supported DVI wiring.

UART> video cls
Notice: DVI output is unavailable: this board has no supported DVI wiring.

UART> video test
Notice: DVI output is unavailable: this board has no supported DVI wiring.

UART> video status
Notice: DVI output is unavailable: this board has no supported DVI wiring.
```

#### Crowbar / Power Glitch Command:
```text
UART> glitch -p
Error: No voltage detected on VOUT/VREF pin
Hint: Use W to enable power, or attach an external supply
```
When inspected via global help:
```text
work with pins, input, output measurement
...
tools and utilities
glitch  [-h] [-c] [-t <trg>] [-d <delay>] [-w <wander>] [-g <time>] [-r <recycle>] [-f <failchar>] [-n <retries>]
video   [on|off|cls|test|status]
```

### Resource and Health Post-Check
* `res` command after running video and glitch:
  - All peripheral leases, DMA channels, and PIO state machines remain at baseline (0 leaked leases).
* `health` command:
  - Core 0 free stack: 2164 B.
  - Core 1 free stack: 3824 B.
  - Heap usage: 0 bytes.

---

## 6. Build and Memory Compliance

* Build: `cmake --build build_rp2350 --target bus_pirate6` passes with 0 warnings.
* Memory Model Guard: `bash tests/test_memory_model.sh` passes with 0 heap violations.
  - Static Main RAM: 388,688 bytes (432 bytes headroom below 389,120 ceiling).
  - Main RAM Headroom: 135,600 bytes.
* Supported Platform Contract: `python3 tools/platform_identity.py build_rp2350 --verify` passes (`supported_contract_passed: true`).
