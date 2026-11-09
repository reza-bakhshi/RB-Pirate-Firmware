# Tasks: Fix ADC and PSU Ownership

- [x] 1. Implement reentrant, owner-aware `adc_busy_wait()` in `src/pirate/amux.c` with nesting and core ID tracking <!-- id: 1.1 -->
- [x] 2. Extend `bp_ui_snapshot_t` in `src/kernel/bp_config.h` and `src/kernel/bp_config.c` with complete pin voltages, PSU states, and publish helper <!-- id: 2.1 -->
- [x] 3. Update `src/system_monitor.c` on Core 0 to publish fresh measurements to `bp_ui_snapshot_t` following `amux_sweep()` <!-- id: 3.1 -->
- [x] 4. Update `src/binmode/bpio.c` to read ADC pin voltages from the atomic snapshot instead of executing invasive hardware `amux_sweep()` <!-- id: 4.1 -->
- [x] 5. Wire PSU overcurrent/undervoltage trip in `src/pirate/psu.c` to abort active Core 1 protocol jobs <!-- id: 5.1 -->
- [x] 6. Create host unit test suite `tests/test_power_monitor.sh` with ASan/UBSan verifying ADC reentrancy, snapshot seqlock consistency, and PSU abort <!-- id: 6.1 -->
- [x] 7. Run OpenSpec validation and all 17 host test suites <!-- id: 7.1 -->
- [x] 8. Build target firmware `build_rp2350/src/bus_pirate6.uf2` and verify RAM/Flash limits <!-- id: 8.1 -->
