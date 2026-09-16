# CC1101 Sub-GHz Mode

The `SUBGHZ` mode connects an external CC1101 transceiver to the Bus Pirate's
buffered I/O header. It supports radio detection, tuning, RSSI measurements,
frequency scans, raw OOK pulse capture, and Princeton-compatible OOK transmit.

## Wiring

| CC1101 | Bus Pirate |
| --- | --- |
| SCK | BIO6 |
| MOSI | BIO7 |
| MISO | BIO4 |
| CS/CSN | BIO5 |
| GDO0 | BIO0-BIO3 (configured at mode setup) |
| GND | GND |
| VCC | 3.3V |

CC1101 modules are 3.3V devices. Do not power the module from 5V. Fit an
antenna intended for the band being used before transmitting.

## Start the mode

Enter interactively:

```text
m subghz
```

Or select a frequency directly, expressed in kHz:

```text
m subghz -f 433920 -g 0 -w 270
```

The supported CC1101 bands are 300-348MHz, 387-464MHz, and 779-928MHz. The
`-w` option selects the 270kHz or 650kHz asynchronous OOK receive bandwidth. The
register profiles match Flipper's canonical OOK270/OOK650 CC1101 presets. The
firmware verifies the CC1101 `PARTNUM` and `VERSION` registers when the mode is
entered and reports a wiring error if the device does not respond.

## Commands

```text
rssi
setfrequency 315000
scan
scan -a 433050 -z 434790 -s 25 -d 10
sweep -a 433050 -z 434790 -s 25 -n 20 -r 85
trace -t 2000
waterfall -a 433050 -z 434790 -s 25 -n 20
receive -t 5000
replay -r 2
record capture.sub -t 5000
load capture.sub
load capture.sub -r 2
load
send 0x123456789abcdef0 -b 64 -t 350 -r 4
```

`scan` defaults to a 4MHz window centered on the configured frequency. `-s`
sets the step in kHz and `-d` the RSSI dwell time in milliseconds. Press any
key to stop a scan early. The strongest measured frequency remains selected.

`receive` prints up to 1024 GDO0 level transitions as `H:microseconds` and
`L:microseconds`. Press any key to stop early. The capture remains in RAM for
`replay`. `record` makes a new capture and saves it as a Flipper-compatible RAW
`.sub` file on the Bus Pirate filesystem. Captures are automatically analyzed
for pulse-length, PWM/Princeton, and Manchester timing. The analysis reports
short and long timing clusters with jitter, estimated bitrate, confidence, and
separate PWM/Princeton and Manchester payload candidates.

`load` reads Flipper RAW `.sub` files using an OOK270 or OOK650 asynchronous
preset, validates and applies the file frequency and receive bandwidth, imports
up to 1024 signed pulse durations into the replay buffer, and analyzes the
signal. Use `-r 1` through `-r 20` to replay the file
immediately; omit `-r` to inspect it without transmitting. Running `load`
without a filename opens a `.sub` file picker on the same onboard FAT volume
that appears as `BUS_PIRATE5`, `BUS_PIRATE6`, or `BUS_PIRATE7` over USB.

`sweep` repeatedly samples each frequency and reports peak RSSI, percentage of
samples above the selected threshold, and a combined confidence score.

`trace` renders 100 time buckets in the terminal (`_` low, `-` high, `*`
transition). `waterfall` renders RSSI over frequency as terminal rows, using
`#`, `O`, `o`, and `.` from strongest to weakest.

`send` emits a Princeton-style OOK frame with payloads up to 64 bits. The
output power table is initialized conservatively to approximately
0dBm. Transmission is not permitted on every CC1101-supported frequency; obey
local band, duty-cycle, and licensing rules.
