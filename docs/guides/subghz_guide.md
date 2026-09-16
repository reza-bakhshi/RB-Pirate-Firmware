# CC1101 Sub-GHz Mode

The `SUBGHZ` mode connects an external CC1101 transceiver to the Bus Pirate's
buffered I/O header. It supports radio detection, tuning, RSSI measurements,
frequency scans, raw OOK pulse capture, live protocol decoding, protocol-aware
transmit, and Flipper `.sub` file interchange (RAW and protocol key files).

Protocol decoders and CC1101 register presets follow Flipper Zero firmware
`lib/subghz` (`dev` branch) timing constants and canonical presets.

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
register profiles match Flipper's canonical OOK270/OOK650 CC1101 presets, plus
2FSK (2.38/12/47.6kHz deviation), GFSK 9.99kBaud, and MSK 99.97kBaud presets.
The firmware verifies the CC1101 `PARTNUM` and `VERSION` registers when the
mode is entered and reports a wiring error if the device does not respond.

## Protocols

Static (decode + transmit, `*` in `protocol` list):

```text
Princeton  Nice FLO  CAME  Holtek HT12x  Linear
Intertechno V3  GateTX  Ansonic  Dooya  SMC5326
```

Dynamic (decode/display only, TX refused as a rolling-code safety rule):

```text
KeeLoq  (serial/button/counter shown; decrypt needs a keystore key)
```

`protocol` with no argument lists all protocols. `protocol <name>` shows bit
lengths, default frequency, and TX capability.

## Commands

```text
rssi
setfrequency 315000
preset Ook650Async
preset import custom.sub
preset export my.sub
protocol
protocol GateTX
info
scan
scan -a 433050 -z 434790 -s 25 -d 10
sweep -a 433050 -z 434790 -s 25 -n 20 -r 85
trace -t 2000
waterfall -a 433050 -z 434790 -s 25 -n 20
hop -f 433920,315000 -d 500 [-s]
receive -t 5000
monitor -t 10000
clone [file.sub] [-t 5000]
replay -r 2
record capture.sub -t 5000
load capture.sub
load capture.sub -r 2
load
load remote.sub -r 10
decode capture.sub
savekey remote.sub
send 0x123456 -b 24 -t 350 -r 4
send -P GateTX -k 0xABCDEF -b 24 -r 10
power -d 0
power -l 96
txallow [strict|open]
bitrate -b 4800
keyload [keeloq_mfcodes]
keys
keyadd MyMfg 0102030405060708 [1]
```

`scan` defaults to a 4MHz window centered on the configured frequency. `-s`
sets the step in kHz and `-d` the RSSI dwell time in milliseconds. Press any
key to stop a scan early. The strongest measured frequency remains selected.

`receive` captures up to 1024 GDO0 level transitions (edges are timestamped
by a GPIO interrupt, so short pulses are not missed while printing), then runs
timing analysis plus every protocol decoder. `monitor` instead decodes live:
each new frame prints once with serial/button detail, duplicates suppressed.
Press any key to stop early.

`clone [file.sub]` is the guided workflow: it captures, analyzes, decodes,
and saves the matched frame as a key file (auto-named `Protocol_Key.sub`
unless a name is given), then prints the replay command. If nothing matches
it suggests a RAW `record`.

`record` streams GDO0 pulses straight to a Flipper-compatible RAW `.sub`
file (no 1024-pulse cap; the tail stays in RAM for `replay`). `load` reads
RAW `.sub` files (any preset, including `Custom` with `Custom_preset_data`
register blobs) and protocol key files (`Protocol:/Bit:/Key:/TE:` plus
optional `Manufacture:`), applies the file frequency/preset, decodes, and
optionally replays with `-r`. `decode` is `load` without transmit. `savekey`
writes the last decoded frame as a Flipper key file for later `load -r`
replay.

`send -P <protocol> -k <hexkey>` builds the frame with the protocol encoder
so TX matches the decoder timing; rolling-code protocols are refused. Plain
`send <payload>` keeps the legacy Princeton path (TE adjustable).

`power -d <dBm>` sets output in dBm (-30..12, PATABLE mapped per Flipper
tables); `power -l` keeps the legacy raw PATABLE byte. `txallow` shows the
transmit policy: `strict` (default) permits TX only on 314.5-315.5,
433.05-434.79, 868.0-868.6, and 902-928 MHz; `open` permits any tunable band.
Every transmit path (`send`, `replay`, `load -r`) is gated by this policy.

`hop -f` captures across up to 8 frequencies; `-s` persists the list to
`subghz_hop.txt`, reloaded on mode entry.

`preset import <file>` loads a Flipper `Custom_preset_data` register blob as
the `Custom` preset (modulation auto-detected from MDMCFG2); `preset export
<file>` dumps the live CC1101 config registers for reuse. `load` also honors
embedded `Custom_preset_data`.

`sweep` repeatedly samples each frequency and reports peak RSSI, percentage of
samples above the selected threshold, and a combined confidence score.

`trace` renders 100 time buckets in the terminal (`_` low, `-` high, `*`
transition). `waterfall` renders RSSI over frequency as terminal rows, using
`#`, `O`, `o`, and `.` from strongest to weakest.

## KeeLoq keys

`keyload <file>` imports `Name,HEXKEY,Type` lines (Flipper `keeloq_mfcodes`
format; types 0 unknown, 1 simple, 2 normal, 3 secure, 4-7 magic). With a
matching key, KeeLoq frames show the manufacturer and counter; without one
they still show serial, button, and hopping code. Transmit of KeeLoq is
always refused.

## Notes

- Captures are automatically analyzed for pulse-length, PWM/Princeton, and
  Manchester timing with jitter/confidence estimates.
- `record`/`load` files live on the same onboard FAT volume that appears as
  `BUS_PIRATE5`, `BUS_PIRATE6`, or `BUS_PIRATE7` over USB.
- Transmission is not permitted on every CC1101-supported frequency; obey
  local band, duty-cycle, and licensing rules.
