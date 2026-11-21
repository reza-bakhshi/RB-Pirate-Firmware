# RB-Pirate Documentation

Documentation for the original RP2350B firmware written from scratch by Reza Bakhshi with a custom architecture. The RB-Pirate hardware was inspired by Dangerous Prototypes' Bus Pirate hardware. Start with the [project README](../readme.md) for the supported board, build command, and flashing procedure.

## Build and first use

| Guide | Purpose |
|---|---|
| [Build system](guides/build_system_guide.md) | Supported platform, CMake options, and output files |
| [System monitor](guides/system_monitor_guide.md) | Voltage, current, and runtime diagnostics |
| [Storage](architecture/storage-guide.md) | microSD storage architecture |
| [Binary modes](guides/binary_mode_guide.md) | Host-facing binary interfaces |
| [LCDTAP](guides/lcdtap_guide.md) | Display-bus capture and reconstruction |
| [Sub-GHz](guides/subghz_guide.md) | CC1101 module workflows |
| [RF24](guides/rf24_guide.md) | nRF24L01+ module workflows |
| [RFID](guides/rfid_guide.md) | PN532 module workflows |

External-module guides require the corresponding hardware. Check device help for the command syntax implemented by your build.

## Architecture and development

| Topic | Reference |
|---|---|
| Core ownership | [Dual-core architecture](architecture/dual_core_guide.md) |
| Cooperative services | [Kernel guide](architecture/kernel-guide.md) |
| Memory allocation | [Memory model](architecture/memory-model.md) |
| Upstream relationship | [Architecture divergence](architecture/upstream-divergence.md) |
| USB and terminal transport | [USB guide](architecture/usb_communication_guide.md) |
| Command implementation | [New command guide](guides/new_command_guide.md), [parsing API](api/bp_cmd_parsing_api.md), [patterns](api/bp_cmd_patterns.md) |
| Protocol modes | [New mode guide](guides/new_mode_guide.md) |
| Buffered pins | [BIO pin guide](guides/bio_pin_guide.md) |
| User interface | [Toolbar guide](guides/new_toolbar_guide.md), [display modes](guides/display_mode_guide.md) |
| Contribution conventions | [Contributing](guides/contributing.md) |

## Validation and historical material

- [Host testing](testing/testing_guide.md)
- [Hardware-in-the-loop rig](testing/hil_test_rig.md)
- [Fixture pipeline](testing/test_fixture_pipeline.md)
- [Audits](audits), [status records](status), and [analysis reports](analysis)
- [License summary](licenses.md) and [third-party license texts](third_party_licenses)

Historical analysis, plans, and imported upstream notes may describe older architectures. For this fork, the build contract in [cmake/bp_platform.cmake](../cmake/bp_platform.cmake) and the active target in [src/CMakeLists.txt](../src/CMakeLists.txt) take precedence over obsolete multi-board build examples.
