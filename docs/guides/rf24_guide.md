# nRF24L01 RF24 Mode

The RF24 mode connects a 3.3V nRF24L01 or nRF24L01+ module to the Bus Pirate. It supports normal packet
transmission and reception plus passive channel-activity measurements using the radio's Received Power Detector
(RPD). Interference and jamming functions are intentionally not included.

## Wiring

| nRF24L01 | Bus Pirate |
|----------|------------|
| VCC      | 3.3V       |
| GND      | GND        |
| SCK      | BIO6       |
| MOSI     | BIO7       |
| MISO     | BIO4       |
| CSN      | BIO5       |
| CE       | BIO0-BIO3 (selected during setup) |

The radio can draw short current spikes. Place a 10uF capacitor close to the module between VCC and GND if the
module resets, disappears, or transmits unreliably. Do not power the module from 5V.

## Enter the mode

Interactive setup:

```text
m rf24
```

Direct setup on channel 76 with CE on BIO0:

```text
m rf24 -c 76 -e 0
```

Mode entry prints the complete pin mapping. It does not probe the radio yet because changing modes turns off the
Bus Pirate power supply. Enable 3.3V after entering RF24 mode, then run a command:

```text
W 3.3
scan
```

The first RF24 command detects and initializes the module. If VOUT is off or the module does not respond, the
firmware prints the wiring again so the connection can be checked without leaving RF24 mode.

RF channel 0 is 2400MHz and channel 125 is 2525MHz.

The complete 0-125 hardware range is available for passive RPD analysis. Packet transmission is restricted to
the commonly allocated 2400-2483.5MHz ISM band: channels 0-83 at 250kbps/1Mbps and channels 1-82 at 2Mbps.
This guard keeps the nominal occupied signal inside the common band, but it does not replace local regulatory
requirements for equipment approval, output power, antennas, or operation.

## Commands

The non-interference RF24 features from ESP32-Bit-Pirate are represented as follows:

| ESP32-Bit-Pirate feature | Bus Pirate RF24 equivalent |
|--------------------------|----------------------------|
| `config`                 | `m rf24` setup and saved settings |
| `setchannel`             | `setchannel` |
| `scan`                   | `scan` |
| `sweep`                  | `sweep` |
| `waterfall`              | Terminal `waterfall` |
| `send`                   | `send` |
| `receive`                | `receive` |

```text
setchannel 40
scan -n 20 -d 160
sweep -n 20 -d 160 -t 5
waterfall -r 10 -n 4 -d 160
```

`scan` reports channels with detected energy and saves the strongest channel. `sweep` reports activity as the
percentage of RPD samples that crossed the detector threshold. `waterfall` prints a terminal view of all 126
channels. RPD detects energy only; it does not identify or decode Wi-Fi, Bluetooth, or other 2.4GHz protocols.

Send a dynamic-length payload to address `e7e7e7e7e7`:

```text
send deadbeef -a e7e7e7e7e7 -c 76 -r 1m -x 16
```

Receive dynamic-length payloads for ten seconds:

```text
receive -a e7e7e7e7e7 -c 76 -p 1 -r 1m -x 16 -t 10000
```

Use `-f 32` on both ends for fixed 32-byte payloads. Use `-N` or `--no-ack` for unacknowledged packets. CRC-off
operation automatically disables acknowledgement because Enhanced ShockBurst acknowledgements require CRC.

Run `<command> -h` for all options and limits.

Only transmit to equipment you control, and follow the power, channel-use, and duty-cycle rules that apply in
your jurisdiction.

The RF24 mode intentionally excludes constant-carrier generation, sweeping noise, beacon spam, and jamming.
Receiving private communications, injecting or replaying packets against equipment without permission, causing
interference, exceeding permitted EIRP, or using unapproved radio hardware may be unlawful. Exact requirements
vary by country; this firmware cannot determine whether a particular transmission is legally authorized.
