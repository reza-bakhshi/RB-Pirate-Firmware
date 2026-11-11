#!/bin/sh
set -e
cd "$(dirname "$0")/.."
for flags in '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
    gcc $flags -std=c11 -pthread -DBP_EMBEDDED -DBP_HOST_TEST -Itests/stubs -Isrc \
        tests/test_control_service.c src/services/bp_control.c src/services/bp_protocol_call.c src/kernel/bp_job.c \
        src/kernel/bp_resource.c -o /tmp/test_control_service
    /tmp/test_control_service
done
python3 - <<'PY'
from pathlib import Path
source = Path('src/pirate.c').read_text()
worker = source.split('static void core1_infinite_loop(void) {',1)[1].split('void core1_entry',1)[0]
for forbidden in ('tud_', 'tx_fifo_service', 'bp_bin_transport_usb_service', 'monitor(', 'rx_uart', 'toolbar_', 'psu_poll'):
    assert forbidden not in worker, forbidden
startup = source.split('static void core1_initialization(void) {',1)[1].split('static void assert_error',1)[0]
assert 'tusb_init' not in startup and 'rx_uart_init_irq' not in startup
assert 'bp_control_poll();' in Path('src/usb_tx.c').read_text()
assert 'bp_control_poll();' in Path('src/usb_rx.c').read_text()
print('[PASS] Worker startup/loop cannot regain USB, UI or monitoring ownership')
PY
