# Spec Delta

## ADDED Requirements

### Requirement: Multi-Block SPI DMA Storage Throughput
The physical storage layer SHALL support multi-block reads (`CMD18`) and multi-block writes (`CMD25`) utilizing hardware SPI DMA channels to maximize sequential transfer speeds.

#### Scenario: Sequential multi-block read
- **GIVEN** a mounted TF card
- **WHEN** reading contiguous sectors exceeding 2048 bytes
- **THEN** sectors are read via SPI multi-block DMA without per-byte CPU polling.

#### Scenario: Sequential multi-block write
- **GIVEN** an active capture file or firmware dump
- **WHEN** streaming multi-sector buffers to disk
- **THEN** data is written via SPI multi-block DMA with pre-erased sector allocation.
