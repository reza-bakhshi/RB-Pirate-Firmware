# Tasks

## 1. CAN Bus Protocol Mode
- [x] 1.1 Port `can2040` PIO assembly and driver into `src/lib/can2040/` and `src/pirate/hwcan_pio.c` under `bp_res` lease arbitration
- [x] 1.2 Implement mode callbacks (`setup`, `cleanup`, `periodic`, `help`, syntax read/write) in `src/mode/can.c`
- [x] 1.3 Implement `can.sniff`, `can.filter`, and `can.stats` commands
- [x] 1.4 Verify CAN mode loopback and acceptance filtering via `tests/test_can_glitch.sh`

## 2. Hardware Fault Injection Engine
- [x] 2.1 Implement `src/commands/global/cmd_glitch.c` and hardware glitch timing sequencer
- [x] 2.2 Implement voltage crowbar pulse control via `CURRENT_EN_OVERRIDE` line
- [x] 2.3 Verify glitch pulse configuration and delay validation in unit test suite
