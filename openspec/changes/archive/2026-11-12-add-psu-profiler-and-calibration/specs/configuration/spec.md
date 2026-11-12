# Spec Delta

## ADDED Requirements

### Requirement: Hardware Calibration Persistence
The configuration subsystem SHALL support loading, validating, and saving board-specific analog calibration profiles to storage, falling back safely to nominal factory defaults if the file is absent or corrupted.

#### Scenario: Valid calibration load
- **GIVEN** a valid `/config/psu_cal.json` file on storage
- **WHEN** the system initializes on boot
- **THEN** gain and offset coefficients are loaded into active PSU translation routines.

#### Scenario: Missing calibration file fallback
- **GIVEN** storage lacks a calibration file
- **WHEN** the system boots
- **THEN** nominal uncalibrated constants are applied without error.
