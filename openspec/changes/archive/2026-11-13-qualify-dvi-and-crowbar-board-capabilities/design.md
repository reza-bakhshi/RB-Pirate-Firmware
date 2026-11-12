# Design: Qualify DVI and dedicated crowbar board capabilities

## Context and current evidence

RB-Pirate defines GPIO16..19 as PSU controls and has no supported DVI connector map or dedicated crowbar output. Earlier code confused current-limit override with a crowbar and advertised incomplete HSTX timing. Software guards now reject those physical capabilities.

Scope: Hardware feasibility and capability gating for optional board revisions/adapters, followed only after qualification by separate physical-output implementations. Core 0 owns PSU safety and capability status; Core 1 owns bounded logic/protocol work; video DMA/IRQ ownership must be explicitly selected.

## Core ownership, state and failure behavior

Require identified board revision, schematic/netlist, adapter wiring, voltage levels and independently checked conflict matrix before enabling a capability. No external DVI/crowbar wiring is known in this session; existing HARDWARE.md inventory does not list those fixtures. Keep unsupported requests fail-closed with no register writes, DMA claims or PSU changes while evidence is absent.

DVI feasibility: check HSTX pin/function availability and routing against all PSU nets, not just BIO labels. Define differential pair electrical interface/termination, connector and clock source without guessing wiring. The future timing backend must describe 640 active + 16 front porch + 96 sync + 48 back porch = 800 horizontal pixels and 480 active + 10 front porch + 2 sync + 33 back porch = 525 lines; derive a documented target pixel clock near 25.175 MHz and its resulting actual frame rate from supported system/HSTX dividers. These are proposed acceptance timing parameters, not a measured output. Produce TMDS control periods during blanking, map each lane and clock polarity, use chained/IRQ-driven finite transfers with explicit count reloads, and define underflow behavior. Move rendering to the inactive row, acknowledge consumption before reuse, and keep Core 0 text updates coherent with the selected rendering owner. Validate under storage/protocol/USB load before claiming 60 Hz-compatible output.

Crowbar feasibility: specify a dedicated external switch and power path with safe gate default, inactive polarity, maximum hardware-enforced pulse, recovery/dead time, allowed rail/load envelope and independent current limiting. A stuck CPU, DMA or IRQ must not leave the switch active; firmware cancellation is insufficient. Current-limit override remains reserved for PSU protection. Measure trigger edge/latency, pulse width distribution and release behavior against a dummy load; do not test on an unidentified target. Future implementation must refuse conflicting pins/PSU states, lease resources transactionally and return inactive on cancel, timeout, reset and partial setup failure.

For a board with no viable wiring, explicitly record UNSUPPORTED as the capability decision; this closes feasibility assessment but does not mark DVI/crowbar implemented. Optional hardware support must be isolated to approved board mappings and have separate acceptance records.

## Resources and rollback

Provide a complete GPIO/function conflict matrix for PSU, storage, debug, HSTX lanes/clock, buffers and trigger/output pins. DVI needs leased serializer/clock/DMA/IRQs and pins. Crowbar needs exclusive dedicated output/trigger resources plus an independently bounded hardware pulse mechanism.

All start/failure/stop paths need an explicit rollback table before implementation. Document queue/message types, producer/consumer core, capacity, ownership-transfer point and late-reply lifetime whenever a new cross-core request is introduced. Completion callbacks must not access a caller buffer after its documented release point.

## Static memory and lifetime

Reuse existing text/attribute and scanline allocations where possible: 2400-byte text, 30-byte row attributes and two 640-byte pixel rows. Quantify packed TMDS/control words, alignment, line descriptors and ISR stack usage before implementation. Do not introduce a full framebuffer; the recorded RP2350 static margin is only 616 bytes before new measurements.

## Prerequisites and exclusions

Driving PSU override as a crowbar, automatically remapping live PSU/debug pins, declaring external hardware present from MCU type, or treating host text rendering as DVI evidence.

No capability is complete merely because configuration accepts it. Hardware gates require independent evidence. New engineering choices and measured timing must be reflected in the corresponding delta scenarios before sign-off.

## Shared verification gates

All new runtime code must use static storage or bounded pools; no malloc/calloc/realloc/free/new/delete or heap-backed STL. RP2350 static main RAM must remain <=389120 bytes, including actual linked reservations measured by the existing memory guard. Baseline in the audit: 388504 bytes, leaving 616 bytes under the ceiling and 135784 bytes main-RAM headroom; scratch stacks are separately reserved at 4 KB/core. These are historical measurements, not a budget approval for future growth. Measure actual byte deltas and stack high-water after each task group.

Core 1 must not print, call FatFS or USB. Use typed bounded queues and published snapshots across cores; queue-full, disconnect, SD hotplug, timeout, cancel and watchdog behavior must be exercised where relevant. No busy wait may be unbounded. No hardware resource claims may bypass bp_res. Startup/teardown must restore leases, GPIO functions, buffer directions, IRQ handlers, clocks and PSU protection.

After each implementation task group, run `cmake --build build_rp2350 --target bus_pirate6`, the named focused suite and `bash tests/test_memory_model.sh`; record flash/RAM and warnings. Final gates: `bash tests/run_all.sh`, a separate full host run forcing GCC/G++ to `-O2 -fsanitize=address,undefined -fno-omit-frame-pointer`, zero-warning supported-platform builds and physical acceptance. Provide a reproducible wrapper/preset for optimized sanitizer flags rather than assuming scripts' existing -O1 runs satisfy -O2. Record each skipped case; a hardware skip cannot satisfy release acceptance. A build named build_rp2040 is not an RP2040 gate without verified platform identity.
