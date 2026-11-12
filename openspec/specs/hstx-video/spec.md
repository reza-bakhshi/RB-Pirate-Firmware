# hstx-video Specification

## Purpose

Define the text rendering interface and the hardware qualification gate for optional RP2350 HSTX DVI output. Physical video remains unsupported on RB-Pirate until safe wiring and a complete measured timing backend exist; host rendering is not proof of physical output.

## Requirements

### Requirement: HSTX Digital Video Generation
On a hardware-qualified board with a supported physical timing backend, the video subsystem SHALL utilize the RP2350 HSTX serializer to generate 640x480 DVI video frames on its designated output lines without starving CPU execution. Unqualified boards SHALL follow the capability gate below.

#### Scenario: Video signal initialization
- **GIVEN** a qualified board and supported physical timing backend
- **WHEN** the user enables external video output via `video on`
- **THEN** the HSTX block generates stable pixel clock and TMDS serialized bitstreams compliant with DVI-D timings.

#### Scenario: Console and waveform mirroring
- **GIVEN** active video generation
- **WHEN** terminal output or logic analyzer captures are rendered
- **THEN** frames update on the external monitor synchronously with internal display buffers.

### Requirement: Board wiring capability gate

A processor having HSTX SHALL NOT by itself imply supported physical DVI output. RB-Pirate lacks a supported DVI connector mapping and uses GPIO16..19 for PSU controls; initialization SHALL report unsupported without claiming DMA channels or programming HSTX. Host renderer simulation SHALL remain separately testable. Physical output requires a verified board pin map and complete timing backend.

#### Scenario: Unsupported board output request
- **WHEN** video output is requested on RB-Pirate without a qualified DVI mapping
- **THEN** initialization reports unsupported without configuring HSTX, claiming DMA or changing PSU pins.
