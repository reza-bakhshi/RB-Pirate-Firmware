# Upstream Divergence & Architecture Record

> Documentation of structural and architectural divergences between RB-Pirate and upstream Bus Pirate 5/6 firmware to guide future merges and upstream collaboration.

---

## 1. Executive Summary

RB-Pirate diverges from upstream Bus Pirate 5 firmware by implementing a complete dual-core architectural refactor:
1. **Core 0 / Core 1 Specialization**: Upstream executed protocols on Core 0 and USB on Core 1. RB-Pirate owns USB, monitoring and toolbars on Core 0; protocol jobs, syntax and BPIO transfers run on Core 1. See dual_core_guide.md for the cooperative control boundary.
2. **Deterministic Static Memory Model**: Upstream utilized dynamic memory allocation (`malloc`, `free`) in FatFS, decoders, and command parsing. RB-Pirate enforces a strict zero-heap policy with fixed static pools, bump arenas, and stack watermarks strictly $\le 380\text{ KB}$ SRAM.
3. **Cooperative Service Kernel**: Replaced unbounded delays with cooperative job slices, timeout aborts, and lease-based hardware resource reclamation.
4. **Lock-Free Binary Transport**: Replaced shared memory blocks with typed, lock-free SPSC queues with monotonic health telemetry.

---

## 2. Upstream Merge Guidelines

When porting upstream features or bug fixes into RB-Pirate:
- **Never Call `malloc`/`free`**: Use static memory pools or place state into the managed `Big Buffer`.
- **Never Call `printf` from Protocol Engines**: Protocol code on Core 1 must use the structured inter-core log ring or publish to UI snapshots.
- **Keep TinyUSB Task/Endpoint Service on Core 0**: Stream binary packets via `bp_bin_transport`.
- **Preserve Command Signatures**: Ensure command names, syntax tokens, and translation keys match upstream for backwards compatibility.
