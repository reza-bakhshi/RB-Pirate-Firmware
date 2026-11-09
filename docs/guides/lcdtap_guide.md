# LCDTap user manual

LCDTap watches the traffic sent to an LCD controller and reconstructs the
display in an RGB565 framebuffer. RB-Pirate can inspect the command stream,
report capture errors, and save the reconstructed image as a standard 24-bit
BMP file on the SD card.

The decoder is based on [Shapoco's LcdTap](https://github.com/shapoco/lcdtap).

## Capabilities and limitations

RB-Pirate supports:

- 4-wire SPI mode 0, MSB first
- 3-wire SPI mode 0 with a 9-bit D/C + data word, MSB first
- I2C slave capture
- ST7789/ST7735, SSD1306, SSD1331, ILI9341/ILI9342/ILI9488, and ST7032
- live command/data statistics and a 256-event command dump
- RGB565 framebuffer export to a 24-bit BMP on the SD card

The dedicated LcdTap-Pico2 board also supports parallel input and DVI,
composite, DisplayLink, browser, and OSD output. Those features depend on pins
and hardware that are not present or available on RB-Pirate. In particular,
8-bit parallel capture requires at least 11 signals, so KS0108 and parallel
ST7032 capture are not available on the eight-pin RB-Pirate connector.

## Before connecting

1. Identify the LCD controller, interface, dimensions, logic voltage, and SPI
   mode from the target schematic or display datasheet.
2. Make sure the target and RB-Pirate share ground.
3. Configure Vout/Vref for the target logic voltage. Do not connect 5V logic
   directly when the target or adapter is only rated for 3.3V.
4. Connect the tap wires in parallel with the existing LCD. The real LCD stays
   connected.
5. Keep SPI wires short and approximately equal in length. Long flying leads
   can distort fast clock edges.

SPI capture is electrically passive. In I2C mode, the RP2350 joins the bus as a
slave at the configured address and generates ACKs, matching the upstream
Pico2 implementation. Avoid I2C capture if the extra slave response could
affect the target system.

## Wiring

| RB-Pirate pin | 4-wire SPI | 3-wire SPI | I2C |
|---|---|---|---|
| IO0 | CS | CS | SDA |
| IO1 | CLK | CLK | SCL |
| IO2 | MOSI | 9-bit MOSI | Not used |
| IO3 | D/C | Not used | Not used |
| IO4 | RST | RST | Not used |
| GND | GND | GND | GND |

`RST` is active low. If the target does not expose reset, leave IO4
disconnected; the software `reset` command can reset the emulated controller.
MISO is not required because LCDTap reconstructs writes sent to the display.

## Quick start

Start a default 240x320 ST7789 capture over 4-wire SPI:

```text
m lcdtap
```

Generate LCD activity on the target, then check the counters:

```text
status
```

Save the reconstructed screen:

```text
save screen.bmp
```

The file appears on the RB-Pirate SD card. Leave the mode with `m hiz` or select
another mode; cleanup releases the DMA channel, PIO state machine, and BIO pins.

## Mode configuration

```text
m lcdtap [-b bus] [-c controller] [-x width] [-y height] [-a address]
```

| Option | Default | Meaning |
|---|---:|---|
| `-b`, `--bus` | `spi4` | `spi4`, `spi3`, or `i2c` |
| `-c`, `--controller` | `st7789` | Controller family to emulate |
| `-x`, `--width` | `240` | Pixel width, or ST7032 text columns |
| `-y`, `--height` | `320` | Pixel height, or ST7032 text rows |
| `-a`, `--address` | `0x3c` | I2C 7-bit slave address; ignored in SPI modes |
| `-h`, `--help` | — | Show mode setup help and examples |

Controller examples:

```text
# ILI9341 portrait framebuffer
m lcdtap -b spi4 -c ili9341 -x 240 -y 320

# ILI9342-style landscape framebuffer
m lcdtap -b spi4 -c ili9341 -x 320 -y 240

# ST7735 using the ST7789-family decoder
m lcdtap -b spi4 -c st7789 -x 128 -y 160

# SSD1306 on I2C address 0x3c
m lcdtap -b i2c -c ssd1306 -x 128 -y 64 -a 0x3c

# SSD1306 using its 4-wire SPI interface
m lcdtap -b spi4 -c ssd1306 -x 128 -y 64

# SSD1331 RGB OLED
m lcdtap -b spi4 -c ssd1331 -x 96 -y 64

# 16-column, 2-row ST7032 text LCD
m lcdtap -b i2c -c st7032 -x 16 -y 2 -a 0x3e
```

ST7032 accepts 2–40 columns, rounded down to an even number by the decoder,
and 1, 2, or 4 rows. The `save` command renders the character cells into a BMP.

## Mode commands

### `status`

Shows decoded command/data bytes, hardware resets, unknown commands, dropped
ring-buffer words, and maximum SPI backlog or I2C hardware FIFO overflows.

```text
Commands: 18, data bytes: 153600, resets: 1, unknown commands: 0, dropped: 0, max backlog: 412
```

A growing data count confirms that traffic is being received. Any nonzero
`dropped` value means the reconstructed frame may contain corrupt or missing
pixels. Shorten wiring, reduce the target bus speed, or reduce other terminal
activity before capturing again.

### `cmddump`

Displays up to 256 captured events:

- `Cxx`: command byte
- `Dxx`: data byte
- `HR`: hardware reset assertion

A stream containing only `Dxx` or only `Cxx` usually means the 4-wire SPI D/C
signal is missing or connected incorrectly.

### `save <file.bmp>`

Writes the current physical framebuffer as an uncompressed 24-bit BMP. An SD
card must be mounted and writable. Existing files with the same name are
replaced.

The framebuffer is write-protected while it is copied. Incoming bus data may
continue filling the capture ring during a slow SD write, so run `status`
afterward and check the dropped counter. For a consistent image, save while
the target display is static.

### `reset`

Resets only the emulated LCD controller and clears its display state. It does
not drive the target's reset pin and does not reset the physical LCD.

### Macros

```text
(0)   show the LCDTap macro list
(1)   show capture status
(2)   reset the emulated controller
```

## Memory requirements

Pixel controllers require `width × height × 2` bytes for the RGB565
framebuffer, in addition to firmware and capture buffers.

| Geometry | Framebuffer RAM | Expected result |
|---:|---:|---|
| 128x64 | 16 KiB | Fits |
| 128x160 | 40 KiB | Fits |
| 240x240 | 113 KiB | Fits |
| 240x320 / 320x240 | 150 KiB | Fits |
| 320x320 | 200 KiB | Tight; depends on the firmware build |
| 320x480 | 300 KiB | Normally too large for internal SRAM |

If allocation fails, setup is cancelled without claiming the capture hardware.

## Troubleshooting

### Counters stay at zero

- Confirm the target is actively updating the LCD after capture starts.
- Check common ground and Vout/Vref.
- Verify IO0/IO1 are connected to CS/CLK rather than similarly named signals.
- For I2C, use the 7-bit address, not an 8-bit read/write address.

### Commands increase but data does not

- Check the D/C connection on IO3 in 4-wire SPI mode.
- Confirm the target uses SPI mode 0 and sends MSB first.
- Select `spi3` only for a true 9-bit D/C + data stream.

### Image has wrong dimensions or wraps

The framebuffer must match the controller's physical address space, not
necessarily the visible glass area. Check CASET/RASET traffic in `cmddump`.

### Colors are wrong

Verify the controller family first. The decoder follows COLMOD, SETREMAP, and
MADCTL commands sent by the target. Wrong colors can indicate the wrong family,
a lost command byte, or capture overruns.

### Image is incomplete or corrupted

Run `status`. If `dropped` is nonzero, reduce SPI speed if possible, shorten
the wires, avoid large terminal output during capture, and run `reset` before
trying again.

### BMP cannot be saved

- Insert and mount a writable SD card.
- Use a valid filename supported by the configured FatFs filesystem.
- Check free storage space.
- Save while the display is static to reduce capture overruns.

## Built-in help

```text
m lcdtap -h       show configuration options and examples
h                 show wiring, limitations, examples, and mode commands
status -h         show status command help
cmddump -h        show command-dump help
save -h           show BMP export help
reset -h          show reset help
```
