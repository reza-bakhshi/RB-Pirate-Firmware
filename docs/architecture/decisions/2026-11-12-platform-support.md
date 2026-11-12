# Preserve and enforce the existing RP2350-only contract

Date: 2026-11-12. Status: implemented build-policy enforcement; physical platform acceptance remains unverified.

## Decision and authority

Enforce the existing supported configuration: RB-Pirate RP2350B, Arm Secure, target `bus_pirate6`. This follows `openspec/config.yaml`, the board's `PICO_RP2350B` declaration and the README's exclusive-board scope. This is enforcement of the established contract, not a new claim of hardware certification or authorization of an RP2040 port.

The SDK accepts generic `rp2350` and normalizes it to `rp2350-arm-s`. Accept both spellings. Keep the legacy `BP_PICO_PLATFORM` option compatible only with these supported values. Validate every explicit option, including conflicts, rather than silently overriding an unsupported request. Default to the supported platform and board only when unspecified. Explicit cached/command-line values take precedence over environment defaults.

Other processors, execution variants and boards must fail before SDK import. Unsupported requests explain the supported flags and that a separate port is required. Do not delete or rename existing build directories: effective configuration and ELF evidence determine their identity. The existing `build_rp2040` directory actually targets RP2350 and is not portability evidence.

## Rationale and alternatives

Silently forcing RP2350 allowed nominal RP2040 CI jobs and local directories to succeed without compiling RP2040 firmware. A real RP2040 port needs independent pin mappings, SDK/accelerator alternatives, resource counts and a memory ceiling appropriate to its smaller SRAM. That work has not been approved or implemented. Removing silent substitution is safer and makes build failures actionable while preserving the supported target.

The supported configure/build preset is `rb-pirate-rp2350`; direct CMake flags remain supported. CodeQL now builds only the supported configuration, and the existing main CI matrix already selects RP2350. README and assistant build instructions match this contract. Historical upstream-analysis prompts are not support declarations and are not rewritten as current product documentation.

## Runtime and resource impact

Validation happens on the build host before SDK/compiler setup. It adds no firmware state, queues, buffers, leases or heap allocation. Protocol behavior and board wiring are unchanged. Firmware static RAM remains subject to the 389120-byte guard. Report actual supported builds rather than counting folder names as separate platforms.

## Verification and remaining work

`tests/test_platform_contract.sh` checks default values, supported generic/normalized aliases, unsupported processors/boards, conflicting legacy flags, unsupported environment defaults and top-level rejection before SDK import. Actual supported preset configuration/build, ELF attributes and memory checks are recorded in the linked OpenSpec evidence.

Machine-readable build identity, binding identity to firmware-info/artifact reporting, complete physical boot acceptance and both core stack high-water remain separate unchecked tasks. A future RP2040 port requires a separate approved change; conditional port tasks are not applicable to this decision. No connected board is required for these build-policy checks, and no physical result is claimed.

See [platform change](../../../openspec/changes/define-supported-platform-contract/proposal.md) and [host evidence](../../../openspec/changes/define-supported-platform-contract/evidence.md).
