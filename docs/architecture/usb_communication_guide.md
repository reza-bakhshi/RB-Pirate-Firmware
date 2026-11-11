# USB and binary communication

TinyUSB initializes on Core 0, which owns its device IRQs and task callbacks.
Core 1 protocol jobs never service TinyUSB. See [core ownership](dual_core_guide.md).

| Interface | Owner | Application path |
| --- | --- | --- |
| CDC 0 terminal | Core 0 | CLI RX/TX queues and worker text/job-input queues |
| CDC 1 binary | Core 0 | Frame parsing, FlatCC response construction and queued USB output |
| MSC disk | Core 0 | Serialized block-device callbacks |

`bp_control_poll()` runs at control service iterations and in terminal/binary wait
paths. Reentrancy suppression prevents a printf or callback from entering `tud_task()`
recursively through the pump. USB/MSC polling is deferred while a block-device lock
is held. UART IRQ input is staged separately before joining the terminal RX queue.

BPIO data transfers are submitted to Core 1 with `bp_protocol_call()`. Core 0 keeps
serving USB while waiting, then builds the response from the completed result buffer.
The callback never receives a FlatCC builder and cannot allocate its buffers on Core 1.

`bp_bin_transport_usb_service()` is the sole binary TX consumer, including disconnect
cleanup, and now runs on Core 0. Endpoint connection queries belong to Core 0. Queue
counters account for saturation; buffering does not guarantee delivery under overload.

Run `bash tests/test_control_service.sh`, `bash tests/test_bin_transport.sh` and
`bash tests/test_flatcc_memory.sh` for host coverage. Physical CDC/MSC stress,
disconnect/reconnect and electrical capture timing remain hardware checks.
