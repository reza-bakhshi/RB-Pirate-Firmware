# INFRARED Specification

## Purpose

Defines the infrared transmit, receive, record, replay, remote and jam behavior exposed by RB-Pirate across raw and protocol-based IR workflows.

## Requirements

### Requirement: Mode activation and teardown

The INFRARED capability SHALL start only when its required hardware is available and SHALL release every owned PIO, DMA, IRQ, clock and pin resource on exit.

#### Scenario: Successful activation
- **WHEN** the required hardware is connected and powered
- **THEN** the mode reports ready and the command prompt changes

#### Scenario: Missing hardware
- **WHEN** the required hardware is absent
- **THEN** the mode reports the missing hardware and does not hang

#### Scenario: Clean exit
- **WHEN** the user leaves the mode
- **THEN** PIO, DMA, IRQ, clock, pin and PSU resources are restored

### Requirement: Command coverage

The INFRARED capability SHALL provide every documented command and SHALL return a result or a clear error for each command.

#### Scenario: irtoy_test command
- **WHEN** the user runs `irtoy_test`
- **THEN** the IR test frame is transmitted and no hang occurs

#### Scenario: tvbgone command
- **WHEN** the user runs `tvbgone`
- **THEN** the TV-B-Gone sequence starts and can be cancelled

#### Scenario: irtx command
- **WHEN** the user runs `irtx`
- **THEN** a raw IR frame is transmitted on the configured TX pin

#### Scenario: irrx command
- **WHEN** the user runs `irrx`
- **THEN** a raw IR frame is captured and displayed

#### Scenario: send command
- **WHEN** the user runs `send`
- **THEN** the selected protocol frame is transmitted

#### Scenario: receive command
- **WHEN** the user runs `receive`
- **THEN** a real remote frame is decoded and displayed

#### Scenario: setprotocol command
- **WHEN** the user runs `setprotocol`
- **THEN** the engine protocol changes and persists

#### Scenario: devicebgone command
- **WHEN** the user runs `devicebgone`
- **THEN** the Device-B-Gone sequence starts and can be cancelled

#### Scenario: remote command
- **WHEN** the user runs `remote`
- **THEN** the built-in remote sends the selected key

#### Scenario: record command
- **WHEN** the user runs `record`
- **THEN** a real remote is recorded to an .ir file

#### Scenario: load command
- **WHEN** the user runs `load`
- **THEN** an .ir file is loaded and its signals are listed

#### Scenario: replay command
- **WHEN** the user runs `replay`
- **THEN** a loaded signal is replayed

#### Scenario: jam command
- **WHEN** the user runs `jam`
- **THEN** IR jamming starts and any key stops it

#### Scenario: config command
- **WHEN** the user runs `config`
- **THEN** the TX/RX pins, protocol and carrier are displayed

#### Scenario: send command with MakeHex protocol
- **WHEN** the user runs `send <dev> <sub> <cmd> -p jvc_mh`
- **THEN** the MakeHex JVC generator encodes the frame and transmits it via the PIO transmitter without collision with native JVC.

#### Scenario: setprotocol command with long protocol name
- **WHEN** the user runs `setprotocol dishplayer_network`
- **THEN** the protocol name is parsed completely without buffer truncation and set as the active protocol.

#### Scenario: irtx command with 16-bit timing validation
- **WHEN** the user runs `irtx` with raw timing values
- **THEN** timings within 1 to 65535 microseconds are transmitted correctly, and timings outside this range or zero values are rejected.

### Requirement: Failure handling

The INFRARED capability SHALL report missing hardware, invalid input and cancellation without hanging the terminal.

#### Scenario: Invalid input
- **WHEN** the user supplies an invalid argument
- **THEN** the command reports the error and returns to the prompt

#### Scenario: Cancellation
- **WHEN** the user presses a key to cancel a long operation
- **THEN** the operation stops and the prompt returns

#### Scenario: USB disconnect
- **WHEN** the USB host disconnects during the operation
- **THEN** the device remains responsive after reconnect

#### Scenario: Invalid raw timing values
- **WHEN** an AIR packet or raw IR frame contains timing durations equal to 0 or exceeding 65535 microseconds
- **THEN** the parser rejects the packet with a clear descriptive error and does not transmit corrupted data.

#### Scenario: Large pair count transmission
- **WHEN** a raw frame with 256 or more mark/space pairs is transmitted via `irio_pio_tx_frame_write()`
- **THEN** the 16-bit loop counter processes all pairs without wrapping or infinite loop hang.

### Requirement: Protocol lookup

The mode SHALL resolve each protocol name to exactly one encoder and SHALL reject duplicate or ambiguous names. All hand-written native encoders and MakeHex/IRP protocol definitions SHALL have unique, unambiguous argument names.

#### Scenario: Protocol lookup
- **WHEN** the behavior related to protocol lookup is exercised
- **THEN** Duplicate protocol names are rejected or resolved explicitly.

#### Scenario: Hand-written protocol lookup
- **WHEN** the user or API searches for a hand-written protocol (`nec`, `sony`, `rc5`, `rc6`, `samsung`, `lg`, `jvc`, `panasonic`, `sharp`, `denon`, `mitsubishi`, `dish`, `sharpk`)
- **THEN** the protocol resolves directly to the hand-written encoder/decoder index without shadowing.

#### Scenario: Disambiguated MakeHex protocol lookup
- **WHEN** the user or API searches for a disambiguated MakeHex protocol name (`jvc_mh`, `panasonic_mh`, `rc5_mh`, `rc6_mh`, `sharp_mh`, `denon_mh`, `mitsubishi_mh`, or their hyphenated aliases)
- **THEN** the protocol resolves directly to the MakeHex protocol table entry.

#### Scenario: Duplicate protocol rejection
- **WHEN** the protocol table is inspected or initialized
- **THEN** zero duplicate argument names exist across the entire protocol table.

### Requirement: Raw transmission safety and timing bounds

The INFRARED transmission engine SHALL validate raw mark and space timings prior to pushing words to the PIO state machine. It SHALL guard against null buffers, zero pair counts, out-of-range modulation frequencies, and empty pair words.

#### Scenario: Guard against empty or null buffers
- **WHEN** `irio_pio_tx_frame_write()` is called with a NULL buffer or zero pairs
- **THEN** the function returns immediately without accessing hardware or waiting for idle.

#### Scenario: Carrier frequency safety clamping
- **WHEN** `irio_pio_tx_frame_write()` is called with an uninitialized or out-of-range carrier frequency (`< 10 kHz` or `> 100 kHz`)
- **THEN** the carrier frequency is clamped to 38 kHz to prevent division-by-zero or illegal PIO clock divisors.
