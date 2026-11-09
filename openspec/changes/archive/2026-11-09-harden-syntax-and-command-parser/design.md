# Design

## Context
See `proposal.md` for motivation. The syntax compiler in `src/syntax_compile.c` converts human-typed syntax lines into executable bytecode slots in `syntax_io.out[]` (bounded to `SYN_MAX_LENGTH` = 256). In prior versions, array boundary checks were evaluated after element emission, permitting one-element overruns when the count equalled `SYN_MAX_LENGTH`. Additionally, repeat attributes of zero (`:0`) bypassed validation, causing underflow loops in execution engines. Correctness backlog items ARCH-700 through ARCH-707 require surgical fixes in Core 0 monitoring, timer management, strict-aliasing, and PIO claim error handling.

## Goals / Non-Goals

**Goals:**
- Enforce strict pre-condition bounds checking before any array write in `syntax_compile.c`.
- Reject invalid `:0` repeats with human-readable error messages.
- Resolve correctness issues ARCH-700, ARCH-702, ARCH-703, ARCH-704, ARCH-705, ARCH-707.
- Implement comprehensive fuzz testing suite `tests/test_parser_fuzz.sh` with AddressSanitizer and UndefinedBehaviorSanitizer.
- Verify command table unification (ARCH-300 / ARCH-301).

**Non-Goals:**
- Redesigning the syntax bytecode instruction set or changing existing syntax mnemonics (`[`, `]`, `r`, `d`, etc.).
- Modifying binary mode protocol packet formats.

## Decisions

### Decision: Check `out_cnt < SYN_MAX_LENGTH` before write
- *Rationale*: Checking array indices before assigning `syntax_io.out[syntax_io.out_cnt]` eliminates any potential off-by-one or buffer overwrite, including during string literal expansion.
- *Alternative*: Resizing arrays dynamically was rejected because RP2350 firmware operates under a strict static memory model without heap allocation.

### Decision: Decouple `current_sense` measurement from boolean enable (ARCH-700)
- *Rationale*: `psu_status.enabled` is a boolean flag indicating whether the PSU is switched on. The measured current from `HW_ADC_CURRENT_SENSE` is a 32-bit integer. Comparing or assigning the integer to a boolean variable truncated the value and prevented proper display updates. Using `s_current_sense_prev` (`uint32_t`) isolates the filter state.

### Decision: Make `system_tick_enable()` idempotent (ARCH-702)
- *Rationale*: Calling `add_repeating_timer_ms` without checking prior active state risks leaking repeating timer structs or triggering spurious callbacks. An atomic boolean guard `s_system_tick_enabled` ensures safe re-enable.

### Decision: Replace `hard_assert` in PIO claim sites with clean error returns (ARCH-705)
- *Rationale*: Hardware assertions halt the processor upon temporary PIO resource exhaustion. Returning error status codes or printing helpful diagnostic notices allows the system to remain responsive and return to the command prompt.

## Risks / Trade-offs
- *Risk*: Malformed inputs during fuzzing might encounter infinite loops in parsing helpers.
  - *Mitigation*: All parsing loops consume at least one character per step or enforce loop iteration bounds with timeouts.
