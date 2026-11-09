# Design: Split Config, Runtime, and UI Snapshot

## Architecture & Data Flow

```
+-------------------------------------------------------------+
|                       Core 0 (Control)                      |
|                                                             |
|  +---------------------+        +------------------------+  |
|  | bp_config_t         |        | bp_runtime_t           |  |
|  | (Persisted JSON)    |        | (Volatile State)       |  |
|  +---------------------+        +------------------------+  |
|            |                                |               |
|            +--------------+-----------------+               |
|                           |                                 |
|                           v                                 |
|               bp_ui_snapshot_publish()                      |
|                           |                                 |
|                           v                                 |
|               +-----------------------+                     |
|               | bp_ui_snapshot_t      |                     |
|               | (Seqlock sequence)    |                     |
|               +-----------------------+                     |
+---------------------------|---------------------------------+
                            | (Lockless Seqlock Read)
                            v
+-------------------------------------------------------------+
|                 Core 1 (Data) / UI Toolbars                 |
|                                                             |
|   bp_ui_snapshot_read() -> Atomic Consistent View           |
+-------------------------------------------------------------+
```

## 1. Structures

### `bp_config_t` (Persisted)
Holds all configuration parameters serialized to `bpconfig.bp`:
- `version`: Configuration schema version.
- `terminal_language`, `terminal_ansi_color`, `terminal_ansi_statusbar`.
- `display_format`.
- `led_effect`, `led_color`, `led_brightness_divisor`.
- `terminal_usb_enable`, `terminal_uart_enable`, `terminal_uart_number`.
- `debug_uart_enable`, `debug_uart_number`.
- `disable_unique_usb_serial_number`, `bpio_debug_enable`.

### `bp_runtime_t` (Volatile)
Holds dynamic operational status:
- Active mode, subprotocol name, `hiz`.
- Storage mount state, errors, filesystem type, capacity.
- Active binary mode, binary queue controls.
- UI flags (cursor visibility, toolbar pause).

### `bp_ui_snapshot_t` (Seqlock Snapshot)
- `seq`: Monotonically increasing sequence number.
- `timestamp_ms`: Snapshot update timestamp.
- `mode`: Current mode ID.
- `vreg_mv`, `vout_mv`, `current_ma`.
- `pin_voltages[HW_PINS]`: ADC voltages on header pins.
- `pin_labels[HW_PINS]`: Current pin function label pointers.
- `pin_func[HW_PINS]`: Pin role enumeration.
- `pwm_active`, `freq_active`, `pullup_enabled`, `open_drain`.

## 2. Seqlock Protocol

### Writer (Core 0 only):
```c
uint32_t s = s_snapshot.seq;
s_snapshot.seq = s + 1; // Mark write in progress (odd)
__dmb();
// Copy updated fields
s_snapshot.timestamp_ms = now_ms;
...
__dmb();
s_snapshot.seq = s + 2; // Mark write complete (even)
```

### Reader (Core 1 or UI Toolbars):
```c
uint32_t s0, s1;
uint32_t retries = 0;
do {
    s0 = s_snapshot.seq;
    while (s0 & 1) { // If write in progress, wait
        tight_loop_contents();
        s0 = s_snapshot.seq;
    }
    __dmb();
    *out = s_snapshot;
    __dmb();
    s1 = s_snapshot.seq;
} while (s0 != s1 && ++retries < 16);
```

## 3. Migration & Fallback

- `bp_config_load()` parses JSON from `bpconfig.bp`.
- If file is missing or unparseable, defaults are loaded immediately with `bp_config_init_defaults()`.
- Version field determines if schema upgrade migrations are necessary.
- Validation checks parameter bounds (e.g. language < COUNT_OF_LANGUAGE_IDX, brightness divisor != 0).
