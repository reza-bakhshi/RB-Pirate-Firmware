# Proposal

## Why
Discrete analog components, sense resistor tolerances, and op-amp offsets cause slight deviations in measured voltage and current across different physical RB-Pirate boards. Currently, the firmware relies on hardcoded linear divisor constants in `src/pirate/psu.c`, with no mechanism for user or factory calibration. In addition, hardware engineers and security researchers frequently need to profile the energy consumption of target devices over time to detect sleep states, cryptographic power anomalies, and battery endurance. Implementing persistent two-point calibration and a continuous Coulomb counter power profiler resolves both needs.

## What Changes
- **Persistent Two-Point PSU Calibration (`w.cal`)**:
  - Implement a guided calibration wizard (`w.cal`) measuring zero-load and reference-load points to calculate gain and offset coefficients for both VREG and current sense.
  - Persist calibration coefficients to `/config/psu_cal.json` on the TF card, automatically loading them on boot and falling back to nominal constants if absent.
- **Continuous Power Profiler & Coulomb Counter (`w.profile`)**:
  - Implement `w.profile` measuring instantaneous voltage, current, power ($P = V \times I$), total charge ($\text{mAh} = \sum I \times \Delta t$), and energy ($\text{mWh}$).
  - Support live interactive status displays and optional real-time CSV logging to `/log/power_<timestamp>.csv` on the TF card.

## Capabilities

### New Capabilities
*(None)*

### Modified Capabilities
- `power-monitor`: Support energy integration, Coulomb counting, and power profiling.
- `configuration`: Persist and restore hardware ADC/DAC calibration profiles.

## Impact
- Core 0 (Control plane): `src/commands/global/w_psu.c`, `src/pirate/psu.c`, `src/system_monitor.c`, `src/services/bp_config.c`.
- Zero heap allocation: Fixed static calibration structures and bounded CSV formatting buffers.
- Improves measurement precision across voltage rails (1.0 V to 5.0 V) and current limits (0 to 500 mA).
