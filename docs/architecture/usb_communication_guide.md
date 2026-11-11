# USB and binary communication

TinyUSB currently runs on **Core 1**, alongside protocol job slices and monitoring.
Core 0 runs the CLI and binary command engines. See [core ownership](dual_core_guide.md).

| Interface | USB task/callback owner | Application path |
| --- | --- | --- |
| CDC 0 terminal | Core 1 | RX queue to Core 0, TX queue from Core 0 |
| CDC 1 binary | Core 1 | SPSC transport queues to/from binary command engines |
| MSC disk | Core 1 | Block-device callbacks; serialize access with storage |

`bp_bin_transport_usb_service()` moves bytes between CDC1 and the transport rings.
Only that consumer drains or discards binary TX bytes. `bp_bin_transport_service()` on
Core 0 does not consume TX bytes. Queue counters report overflow and disconnected drops.
Core 1 text goes through `bp_term_core1_putc()` and Core 0 drains it to the terminal queue.

Keep protocol slices bounded: blocking a job also blocks the next TinyUSB service iteration.
SPSC queues provide buffering; they do not guarantee latency or complete packet delivery.

Run host transport tests with `bash tests/test_bin_transport.sh`. Physical USB stress,
MSC concurrency, host backpressure and disconnect/reconnect still require a device.
The HIL smoke harness does not implement CDC1 framing stress.
