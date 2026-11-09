# Mode Lifecycle Architecture

> Developer guide to hardware protocol modes in RB-Pirate: registration, transactional initialization, runtime execution, and fault rollback.

---

## 1. Mode Design Philosophy

In RB-Pirate, hardware protocol modes (such as 1-WIRE, UART, I2C, SPI, 2WIRE, 3WIRE, and LCDTAP) run under explicit contracts:
- **Zero Heap**: Modes allocate state statically or acquire temporary scratch memory from the managed `Big Buffer` (128 KB) via `bp_res`.
- **Transactional State Transitions**: Moving between modes never leaves hardware pins in an indeterminate floating or driven state.

---

## 2. Mode Contract & Function Table

Each mode provides a standardized descriptor `struct _mode_t` defined in `src/mode/bp_mode.h`:

```c
typedef struct _mode_t {
    const char* name;
    uint32_t (*protocol_init)(void);
    uint32_t (*protocol_cleanup)(void);
    void (*protocol_settings)(void);
    void (*protocol_help)(void);
    void (*protocol_periodic)(void);
    uint32_t (*protocol_read)(void);
    uint32_t (*protocol_write)(uint32_t data);
    void (*protocol_start)(void);
    void (*protocol_stop)(void);
} mode_t;
```

---

## 3. Transactional Mode Transition Sequence

When a user switches modes (e.g., typing `m 5` for SPI):

1. **Active Job Pre-check**: Core 0 verifies that no background capture, dump, or sniffer job is actively streaming. Any active job is cancelled and its leases reclaimed.
2. **Peripheral Teardown**: Core 1 invokes `protocol_cleanup()` of the outgoing mode:
   - PIO programs are uninstalled from instruction memory.
   - PIO state machines, DMA channels, and IRQs are released back to `bp_res`.
   - Dedicated IO buffer directions are set back to HiZ.
3. **Hardware Initialization**: Core 1 invokes `protocol_init()` of the incoming mode.
4. **Failure Rollback**: If `protocol_init()` fails or returns an error, Core 1 automatically invokes HiZ mode initialization (`hiz_init()`), ensuring pins return to high impedance without user intervention.
