# Tasks

## 1. Two-Point Calibration Wizard
- [x] 1.1 Implement `w.cal` interactive wizard measuring zero and reference points
- [x] 1.2 Implement JSON serialization and loading for `/config/psu_cal.json`
- [x] 1.3 Apply gain and offset corrections to `psu_measure()` and DAC output

## 2. Power Profiler and Coulomb Counter
- [x] 2.1 Implement `w.profile` integrating current into milliampere-hours ($\text{mAh}$) and milliwatt-hours ($\text{mWh}$)
- [x] 2.2 Implement real-time CSV streaming export to TF card
- [x] 2.3 Verify integration accuracy against a known resistive load
