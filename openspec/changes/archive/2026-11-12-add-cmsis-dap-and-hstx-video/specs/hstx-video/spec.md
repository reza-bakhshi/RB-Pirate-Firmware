# Spec Delta

## ADDED Requirements

### Requirement: HSTX Digital Video Generation
The video subsystem SHALL utilize the RP2350 HSTX serializer to generate 640x480 DVI video frames on designated high-speed output lines without starving CPU execution.

#### Scenario: Video signal initialization
- **WHEN** the user enables external video output via `video on`
- **THEN** the HSTX block generates stable pixel clock and TMDS serialized bitstreams compliant with DVI-D timings.

#### Scenario: Console and waveform mirroring
- **GIVEN** active video generation
- **WHEN** terminal output or logic analyzer captures are rendered
- **THEN** frames update on the external monitor synchronously with internal display buffers.
