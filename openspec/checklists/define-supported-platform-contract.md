# Define and enforce the supported platform contract acceptance checklist

- Source: local audit and pinned upstream sources from [FEATURES.md](../FEATURES.md); implementation comparison must record the exact source commit before execution.
- Local implementation: planned in [proposal](../changes/define-supported-platform-contract/proposal.md), [design](../changes/define-supported-platform-contract/design.md), [tasks](../changes/define-supported-platform-contract/tasks.md).
- Hardware required: per-case fixtures below and [HARDWARE.md](../HARDWARE.md); availability must be confirmed at execution time.
- Firmware build / tester / execution date: not yet captured; record actual values for every run.
- Overall result: **UNVERIFIED — acceptance not executed**. Physical rows are **BLOCKED pending identified connected fixtures and captured evidence**. Existing host passes are historical evidence only.

Host progress (2026-11-12): supported preset configure/build, platform rejection tests, cache/compile/ELF inspection and memory guard passed; see [evidence](../changes/define-supported-platform-contract/evidence.md). Physical rows and the full feature sign-off remain unverified.

## 1. Preconditions

- [ ] Record exact firmware commit plus dirty-tree patch identity, board revision, SDK/toolchain, build platform, feature flags, fixture model and wiring.
- [ ] Record baseline supported build, normal and optimized sanitizer host results, memory footprint, both core stack high-water and health/resource counters.
- [ ] Confirm correct VOUT/voltage/current limits, shared ground and no debug/UART/pin conflicts; identify missing fixtures explicitly.
- [ ] Enumerate every command, subcommand and option branch affected by this change from the actual command tables; expand the family rows below into individual executed rows before sign-off.

## 2. Command and behavior checks

| Planned command/behavior family | Fixture/stimulus | Expected observable result | Actual/evidence/status |
|---|---|---|---|
| Configure/build identity | Supported and unsupported platform/board combinations | Requests are honored or rejected; reported silicon matches artifacts. | Not executed; evidence absent; UNVERIFIED/BLOCKED for hardware. |
| Firmware info and capability errors | Supported board plus unsupported capability commands | Identity is accurate and unavailable hardware is reported explicitly. | Not executed; evidence absent; UNVERIFIED/BLOCKED for hardware. |
| Conditional RP2040 support | Only after a separately approved port | Independent compile/link/memory/boot/protocol evidence; otherwise NOT APPLICABLE, never PASS. | Not executed; evidence absent; UNVERIFIED/BLOCKED for hardware. |

For every executed row, record literal input, initial state, expected deadline/threshold, actual measured value/status, serial log and independent fixture/scope artifact. A pure-software row can pass with host evidence; a physical row cannot.

## 3. Error and cancellation checks

- [ ] Queue full and resource exhaustion have bounded errors and no partial ownership.
- [ ] Missing module, wrong wiring and inappropriate VOUT refuse unsafe operation; do not apply destructive stimuli to unidentified hardware.
- [ ] Cancel, timeout, repeated cancel and USB disconnect/reconnect restore documented state.
- [ ] SD removal/replacement during affected I/O reports media errors and invalidates stale requests.
- [ ] Stalled worker/peripheral and watchdog cases preserve diagnostics; identify nonapplicable cases with rationale rather than marking them pass.

## 4. Lifecycle checks

- [ ] Enter/start, exercise every affected command, stop/cancel, restart and switch to another mode.
- [ ] Run the required repeated-cycle count (1000 for board lifecycle qualification; at least 100 for other affected features).
- [ ] Compare PIO/SM, DMA, IRQ, timer/peripheral/buffer leases, pin functions/directions, clocks and PSU protection with the baseline.
- [ ] Verify no source-buffer reuse before physical transfer completion; reset leaves safe outputs and recoverable persistence.

## 5. Regression checks

- [ ] Terminal and toolbars remain responsive under concurrent USB/MSC, protocol and display/monitor load.
- [ ] Storage remounts; other modes and captures still work; no unexplained watchdog resets.
- [ ] Run the unified host suite and supported zero-warning builds; record skipped hardware tests separately.
- [ ] Record measured flash/RAM delta and stack high-water; main RAM remains within the applicable platform ceiling.

## 6. Sign-off

- [ ] All individual command/behavior rows have evidence and meet their deadlines/thresholds.
- [ ] All errors, lifecycle and regression cases pass; blocked requirements remain visibly open.
- [ ] Reviewer records accepted limitations and artifact identifiers; FEATURES.md is updated only for capabilities that meet the full evidence gate.
- [ ] Change tasks are complete and archive includes memory/stack measurements and stability results.
