# Spec Delta

## MODIFIED Requirements

### Requirement: Raw transmission safety and timing bounds

The INFRARED transmission engine SHALL validate raw mark and space timings prior to pushing words to the PIO state machine. It SHALL guard against null buffers, zero pair counts, out-of-range modulation frequencies, empty pair words, and unbounded delimiter scanning in untrusted packet strings.

#### Scenario: Guard against empty or null buffers
- **WHEN** `irio_pio_tx_frame_write()` is called with a NULL buffer or zero pairs
- **THEN** the function returns immediately without accessing hardware or waiting for idle.

#### Scenario: Carrier frequency safety clamping
- **WHEN** `irio_pio_tx_frame_write()` is called with an uninitialized or out-of-range carrier frequency (`< 10 kHz` or `> 100 kHz`)
- **THEN** the carrier frequency is clamped to 38 kHz to prevent division-by-zero or illegal PIO clock divisors.

#### Scenario: Bounded delimiter scan
- **WHEN** an AIR packet string lacks expected delimiters (`$`, `:`, `,`, `;`)
- **THEN** the parser aborts cleanly with an error message without exceeding input buffer boundaries.

#### Scenario: Array bounds enforcement
- **WHEN** an AIR packet contains more than 128 mark/space timing pairs
- **THEN** the parser rejects the packet before any out-of-bounds array write occurs.
