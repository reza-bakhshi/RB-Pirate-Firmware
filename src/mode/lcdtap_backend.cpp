/**
 * @file lcdtap_backend.cpp
 * @brief RB-Pirate hardware adapter for Shapoco's LcdTap library.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Bus Pirate project contributors
 */

#include "mode/lcdtap_backend.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "lcdtap/lcdtap.hpp"
#include "lcdtap/pico2/i2c_slave.hpp"
#include "lcdtap/pico2/spi_slave.hpp"
#include "spi_3line_mode0.pio.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace {

constexpr uint kPinCs = 8;       // BIO0
constexpr uint kPinSclk = 9;     // BIO1
constexpr uint kPinMosi = 10;    // BIO2
constexpr uint kPinDc = 11;      // BIO3 (must immediately follow MOSI)
constexpr uint kPinReset = 12;   // BIO4
constexpr uint32_t kRingLog2 = 14;
constexpr uint32_t kRingBytes = 1u << kRingLog2;
constexpr uint32_t kRingWords = kRingBytes / sizeof(uint32_t);

static_assert(kPinCs == SPI_CS_PIN, "LCDTap PIO CS pin does not match BIO0");
static_assert(kPinSclk == SPI_SCLK_PIN, "LCDTap PIO clock pin does not match BIO1");
static_assert(kPinDc == kPinMosi + 1u, "LCDTap SPI4 requires adjacent MOSI and D/C pins");

alignas(kRingBytes) uint32_t gSpiRing[kRingWords];
uint32_t gI2cRing[1024];
lcdtap::pico2::SpiSlaveState gSpi = {};
lcdtap::pico2::I2cSlaveState gI2c = {};
lcdtap::LcdTap *gTap = nullptr;
lcdtap_backend_config_t gConfig = {};
int gSpiSm = -1;
bool gRunning = false;
bool gRawIrqInstalled = false;

void *frameAlloc(size_t size) { return std::malloc(size); }
void frameFree(void *ptr) { std::free(ptr); }

void tapLog(void *, const char *message) {
    if (message) printf("LCDTap: %s\r\n", message);
}

void __not_in_flash_func(spiGpioIrq)() {
    uint32_t csEvents = gpio_get_irq_event_mask(kPinCs);
    if (csEvents) {
        gpio_acknowledge_irq(kPinCs, csEvents);
        if (gRunning && gSpi.dmaCh >= 0) lcdtap::pico2::spiSlaveResetSm(&gSpi);
    }

    uint32_t resetEvents = gpio_get_irq_event_mask(kPinReset);
    if (resetEvents) {
        gpio_acknowledge_irq(kPinReset, resetEvents);
        if (gTap) gTap->inputReset(!gpio_get(kPinReset));
        if (gRunning && gSpi.dmaCh >= 0) lcdtap::pico2::spiSlaveResetSm(&gSpi);
    }
}

void initSpi3() {
    gSpi.progOffset = pio_add_program(gSpi.cfg.pio, &spi_3line_mode0_program);
    gSpi.pioProgram = &spi_3line_mode0_program;

    pio_sm_config smConfig = spi_3line_mode0_program_get_default_config(gSpi.progOffset);
    sm_config_set_in_pins(&smConfig, gSpi.cfg.pinMosi);
    sm_config_set_in_shift(&smConfig, false, false, 32);
    sm_config_set_fifo_join(&smConfig, PIO_FIFO_JOIN_RX);
    sm_config_set_jmp_pin(&smConfig, gSpi.cfg.pinCs);
    pio_sm_init(gSpi.cfg.pio, gSpi.cfg.sm, gSpi.progOffset, &smConfig);
    pio_sm_set_enabled(gSpi.cfg.pio, gSpi.cfg.sm, true);
    lcdtap::pico2::spiSlaveInitDma(&gSpi);
}

void put16(uint8_t *dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t *dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

bool writeAll(FIL *file, const void *data, UINT size) {
    UINT written = 0;
    return f_write(file, data, size, &written) == FR_OK && written == size;
}

}  // namespace

extern "C" bool lcdtap_backend_start(const lcdtap_backend_config_t *config) {
    if (!config || config->bus > LCDTAP_BUS_SPI3 || config->controller > LCDTAP_CTRL_KS0108 ||
        config->width == 0 || config->height == 0 || config->width > 480 || config->height > 480) {
        return false;
    }

    lcdtap_backend_stop();
    gConfig = *config;

    lcdtap::LcdTapConfig tapConfig;
    lcdtap::getDefaultConfig(static_cast<lcdtap::ControllerFamily>(config->controller), &tapConfig);
    tapConfig.busInterface = static_cast<lcdtap::BusType>(config->bus);
    tapConfig.buffWidth = static_cast<uint16_t>(config->width);
    tapConfig.buffHeight = static_cast<uint16_t>(config->height);
    if (config->controller == LCDTAP_CTRL_ST7032) {
        tapConfig.textCols = static_cast<uint8_t>(config->width);
        tapConfig.textRows = static_cast<uint8_t>(config->height);
    }
    tapConfig.i2cSlaveAddr = static_cast<uint8_t>(config->i2c_address);
    tapConfig.dviWidth = tapConfig.buffWidth;
    tapConfig.dviHeight = tapConfig.buffHeight;

    lcdtap::HostInterface host = {frameAlloc, frameFree, tapLog, nullptr};
    gTap = new (std::nothrow) lcdtap::LcdTap(tapConfig, host);
    if (!gTap || gTap->getStatus() != lcdtap::Status::OK) {
        delete gTap;
        gTap = nullptr;
        printf("LCDTap framebuffer allocation failed (%lux%lu RGB565).\r\n",
               static_cast<unsigned long>(config->width), static_cast<unsigned long>(config->height));
        return false;
    }

    if (config->bus == LCDTAP_BUS_I2C) {
        lcdtap::pico2::I2cSlaveConfig i2cConfig = {
            i2c0, kPinCs, kPinSclk, static_cast<uint8_t>(config->i2c_address)};
        lcdtap::pico2::i2cSlaveInit(&gI2c, i2cConfig, gI2cRing,
                                    sizeof(gI2cRing) / sizeof(gI2cRing[0]));
        gI2c.inst = gTap;
    } else {
        gSpiSm = pio_claim_unused_sm(pio1, false);
        if (gSpiSm < 0) {
            printf("LCDTap could not claim a PIO state machine.\r\n");
            delete gTap;
            gTap = nullptr;
            return false;
        }

        lcdtap::pico2::SpiSlaveConfig spiConfig = {
            pio1, static_cast<uint>(gSpiSm), kPinCs, kPinSclk, kPinMosi, kPinDc, kRingLog2};
        if (config->bus == LCDTAP_BUS_SPI4) {
            lcdtap::pico2::spiSlaveInit(&gSpi, spiConfig, gSpiRing, kRingWords);
        } else {
            gSpi.cfg = spiConfig;
            gSpi.ringBuf = gSpiRing;
            gSpi.ringWords = kRingWords;
            gSpi.dmaCh = -1;
            gSpi.readIdx = 0;
            initSpi3();
        }
        gSpi.inst = gTap;

        uint64_t irqMask = (1ull << kPinCs) | (1ull << kPinReset);
        gpio_add_raw_irq_handler_masked64(irqMask, spiGpioIrq);
        gRawIrqInstalled = true;
        gpio_set_irq_enabled(kPinCs, GPIO_IRQ_EDGE_RISE, true);
        gpio_set_irq_enabled(kPinReset, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        irq_set_enabled(IO_IRQ_BANK0, true);
        gTap->inputReset(!gpio_get(kPinReset));
    }

    gRunning = true;
    return true;
}

extern "C" void lcdtap_backend_stop(void) {
    if (gConfig.bus == LCDTAP_BUS_I2C) {
        if (gRunning) lcdtap::pico2::i2cSlaveDeinit(&gI2c);
    } else {
        gpio_set_irq_enabled(kPinCs, GPIO_IRQ_EDGE_RISE, false);
        gpio_set_irq_enabled(kPinReset, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        if (gRawIrqInstalled) {
            gpio_remove_raw_irq_handler_masked64((1ull << kPinCs) | (1ull << kPinReset), spiGpioIrq);
            gRawIrqInstalled = false;
        }
        if (gRunning && gSpi.dmaCh >= 0) lcdtap::pico2::spiSlaveDeinit(&gSpi);
        if (gSpiSm >= 0) {
            pio_sm_unclaim(pio1, static_cast<uint>(gSpiSm));
            gSpiSm = -1;
        }
    }

    gRunning = false;
    delete gTap;
    gTap = nullptr;
    gSpi.inst = nullptr;
    gI2c.inst = nullptr;
}

extern "C" void lcdtap_backend_process(void) {
    if (!gRunning || !gTap) return;
    if (gConfig.bus == LCDTAP_BUS_I2C) lcdtap::pico2::i2cSlaveProcess(&gI2c);
    else lcdtap::pico2::spiSlaveProcess(&gSpi);
    gTap->tick(to_ms_since_boot(get_absolute_time()));
}

extern "C" void lcdtap_backend_reset(void) {
    if (!gTap) return;
    gTap->inputReset(true);
    gTap->inputReset(false);
}

extern "C" void lcdtap_backend_status(void) {
    if (!gTap) {
        printf("LCDTap is not running.\r\n");
        return;
    }
    lcdtap_backend_process();
    printf("Commands: %lu, data bytes: %lu, resets: %lu, unknown commands: %lu",
           static_cast<unsigned long>(gTap->getRxCmdBytes()),
           static_cast<unsigned long>(gTap->getRxDataBytes()),
           static_cast<unsigned long>(gTap->getHwResetCount()),
           static_cast<unsigned long>(gTap->getUnknownCmdCount()));
    if (gConfig.bus == LCDTAP_BUS_I2C) {
        printf(", dropped: %lu, I2C overflows: %lu\r\n",
               static_cast<unsigned long>(gI2c.dropWords),
               static_cast<unsigned long>(gI2c.hwOverflowCount));
    } else {
        printf(", dropped: %lu, max backlog: %lu\r\n",
               static_cast<unsigned long>(gSpi.dropWords),
               static_cast<unsigned long>(gSpi.backlogMaxWords));
    }
}

extern "C" void lcdtap_backend_dump(void) {
    if (!gTap) return;
    lcdtap_backend_process();
    const uint16_t *data = gTap->dumpGetBuffer();
    uint16_t size = gTap->dumpGetSize();
    if (!size) {
        printf("No LCD commands captured yet.\r\n");
        return;
    }
    for (uint16_t i = 0; i < size; i++) {
        if ((i & 0x0fu) == 0) printf("%04x: ", i);
        if (data[i] == lcdtap::LcdTap::DUMP_EVENT_HW_RESET) printf("HR   ");
        else printf("%c%02x  ", (data[i] & 0x100u) ? 'D' : 'C', data[i] & 0xffu);
        if ((i & 0x0fu) == 0x0fu || i + 1 == size) printf("\r\n");
    }
}

extern "C" bool lcdtap_backend_save_bmp(const char *filename) {
    if (!gTap || !filename || !*filename) return false;
    lcdtap_backend_process();

    lcdtap::LcdTapConfig cfg = gTap->getConfig();
    const uint32_t rowBytes = (static_cast<uint32_t>(cfg.buffWidth) * 3u + 3u) & ~3u;
    const uint32_t imageBytes = rowBytes * cfg.buffHeight;
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, 54u + imageBytes);
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, cfg.buffWidth);
    put32(header + 22, cfg.buffHeight);
    put16(header + 26, 1);
    put16(header + 28, 24);
    put32(header + 34, imageBytes);

    FIL file;
    FRESULT result = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        printf("Unable to create %s (FatFs error %u).\r\n", filename, result);
        return false;
    }

    static uint8_t row[480u * 3u + 3u];
    uint16_t *frame = gTap->getFramebuf();
    gTap->setWriteProtected(true);
    bool ok = writeAll(&file, header, sizeof(header));
    for (int32_t y = static_cast<int32_t>(cfg.buffHeight) - 1; ok && y >= 0; y--) {
        std::memset(row, 0, rowBytes);
        for (uint32_t x = 0; x < cfg.buffWidth; x++) {
            uint16_t pixel = frame[static_cast<uint32_t>(y) * cfg.buffWidth + x];
            row[x * 3u + 0u] = static_cast<uint8_t>((pixel & 0x1fu) * 255u / 31u);
            row[x * 3u + 1u] = static_cast<uint8_t>(((pixel >> 5) & 0x3fu) * 255u / 63u);
            row[x * 3u + 2u] = static_cast<uint8_t>(((pixel >> 11) & 0x1fu) * 255u / 31u);
        }
        ok = writeAll(&file, row, rowBytes);
    }
    gTap->setWriteProtected(false);
    if (f_close(&file) != FR_OK) ok = false;
    if (ok) printf("Saved %ux%u LCD capture to %s.\r\n", cfg.buffWidth, cfg.buffHeight, filename);
    else printf("Failed while writing %s.\r\n", filename);
    return ok;
}
