# Feature Acceptance Checklists

One file per feature. Use the template at
[../FEATURE_ACCEPTANCE_CHECKLIST.md](../FEATURE_ACCEPTANCE_CHECKLIST.md).

A feature is not `verified` in [../FEATURES.md](../FEATURES.md) until its checklist is complete,
signed off, and the evidence is linked.

| Checklist | Spec | Owner change | Current result |
|---|---|---|---|
| [infrared.md](infrared.md) | [infrared](../specs/infrared/spec.md) | F3 | READY (hardware available) |
| [subghz.md](subghz.md) | [subghz](../specs/subghz/spec.md) | F4 | READY (hardware available) |
| [rfid.md](rfid.md) | [rfid](../specs/rfid/spec.md) | F5 | READY (hardware available) |
| [rf24.md](rf24.md) | [rf24](../specs/rf24/spec.md) | F6 | READY (hardware available; WiFi sweep limitation documented) |
| [lcdtap.md](lcdtap.md) | [lcdtap](../specs/lcdtap/spec.md) | F7 | READY (hardware available) |
| [cell.md](cell.md) | [cell](../specs/cell/spec.md) | F8 | READY (hardware available) |
| [ps2.md](ps2.md) | [ps2-sniffer](../specs/ps2-sniffer/spec.md) | F9 | READY (hardware available) |
| [usb-sniffer.md](usb-sniffer.md) | [usb-sniffer](../specs/usb-sniffer/spec.md) | F9 | READY (hardware available) |
| [base-modes.md](base-modes.md) | [base-modes](../specs/base-modes/spec.md) | A4/B7 | READY (hardware available) |
| [binary-modes.md](binary-modes.md) | [binary-modes](../specs/binary-modes/spec.md) | B6 | READY (hardware available) |

Update the result to `PASS`, `FAIL` or `BLOCKED` as the audit progresses. A `FAIL` must name the
command, expected result, actual result and defect owner change.
