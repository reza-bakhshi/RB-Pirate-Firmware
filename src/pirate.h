// global config file
#ifndef BP_CONFIG
#define BP_CONFIG

// uncomment to disable unique com ports for manufacturing testing
#define BP_MANUFACTURING_TEST_MODE

#define BP_FIRMWARE_VERSION "main branch"

#ifndef BP_FIRMWARE_HASH // this variable is for the current commit GIT hash
#define BP_FIRMWARE_HASH "unknown"
#endif
#ifndef BP_FIRMWARE_TIMESTAMP // this variable is for the compile timestamp
    #define BP_FIRMWARE_TIMESTAMP ( __DATE__ " " __TIME__ )
#endif

#define BP_SPI_START_SPEED 1000 * 1000
#define BP_SPI_HIGH_SPEED 1000 * 1000 * 16
#define BP_SPI_SHIFT_SPEED 1000 * 1000 * 16
#define BP_SYSTEM_TICK_MS -500
// #define XSTR(x) STR(x)
// #define STR(x) #x
// #pragma message "BP_FIRMWARE_HASH value:" XSTR(BP_FIRMWARE_HASH)
#define BP_FILENAME_MAX 13

// UI stuff
#define UI_CMDBUFFSIZE 512 // must be power of 2

// USB VID/PID
#define USB_VID 0x1209
#define USB_PID 0x7331
#define USB_VENDOR "BusPirate.com"
#define USB_PRODUCT "BusPirate5"

// enable modes
#define BP_USE_HW1WIRE
#define BP_USE_HWUART
#define BP_USE_HWHDUART
#define BP_USE_HWI2C
#define BP_USE_HWSPI
#define BP_USE_HW2WIRE
#define BP_USE_HW3WIRE
#define BP_USE_HWLED
#define BP_USE_DIO
#define BP_USE_INFRARED
#define BP_USE_SUBGHZ
#define BP_USE_RFID
// #define 	BP_USE_DUMMY1
// #define 	BP_USE_DUMMY2
// #define     BP_USE_BINLOOPBACK
#define     BP_USE_JTAG
//#define BP_USE_I2S
// #define     BP_USE_USBPD
 #define     BP_USE_PS2
 #define     BP_USE_USB

#define BIG_BUFFER_SIZE (128 * 1024)


#define RP2350 2350


#include "platform/bpi6-rev2.h"
#define RPI_PLATFORM RP2350
#define BP_HW_STORAGE_TFCARD 1
#define BP_HW_IOEXP_NONE 1
#define BP_HW_FALA_BUFFER 1
#define BP_HW_PSU_PWM 1
#define BP_HW_RP2350_E9_BUG 1

#include "translation/base.h"
#include "printf-4.0.0/printf.h"
#include <stdint.h>
#include <stdbool.h>
#include "debug_rtt.h"

void system_tick_enable(int16_t repeat_interval);
void system_tick_disable(void);

#define spi_busy_wait(ENABLE) spi_busy_wait_internal(ENABLE, __FILE__, __LINE__)
void spi_busy_wait_internal(bool enable, const char *file, int line);

//#define BP_PIO_SHOW_ASSIGNMENT

#define PIO_RGB_LED_PIO pio2
#define PIO_RGB_LED_SM 0

#define PIO_LOGIC_ANALYZER_PIO pio0
#define PIO_LOGIC_ANALYZER_SM 0

#define PIO_MODE_PIO pio1
// all SM reserved for mode

// 1wire settings
#define M_OW_OWD BIO0

// UART settings
#define M_UART_PORT uart0
#define M_UART_GLITCH_TRG BIO0
#define M_UART_GLITCH_RDY BIO1
#define M_UART_TX BIO4
#define M_UART_RX BIO5
#define M_UART_CTS BIO6
#define M_UART_RTS BIO7
#define M_UART_RXTX BIO0

// i2c settings
#define M_I2C_SDA BIO0
#define M_I2C_SCL BIO1

// SPI settings
#define M_SPI_PORT spi1
#define M_SPI_CLK BIO6
#define M_SPI_CDO BIO7
#define M_SPI_CDI BIO4
#define M_SPI_CS BIO5
#define M_SPI_SELECT 0
#define M_SPI_DESELECT 1

// 2wire settings
#define M_2WIRE_SDA BIO0
#define M_2WIRE_SCL BIO1
#define M_2WIRE_RST BIO2

// 3wire settings
#define M_3WIRE_MOSI BIO7
#define M_3WIRE_SCLK BIO6
#define M_3WIRE_MISO BIO4
#define M_3WIRE_CS BIO5
#define M_3WIRE_SELECT 0
#define M_3WIRE_DESELECT 1

// LED settings
#define M_LED_SDO BIO0
#define M_LED_SCL BIO1 // only used on APA102

typedef struct {
    bool debug;                 // Debug flag for BPIO
    uint32_t speed;              // Speed in Hz or baud for the mode
    uint8_t data_bits;           // Data bits for the mode (e.g., 8 for UART)
    bool parity;                 // Parity for the mode (true for even parity, false for no parity)
    uint8_t stop_bits;           // Stop bits for the mode (1 or 2)
    bool flow_control;           // Flow control for the mode (true for enabled, false for disabled)
    bool signal_inversion;       // Signal inversion for the mode (true for inverted, false for normal)
    bool clock_stretch;          // Clock stretching for I2C mode (true for enabled, false for disabled)
    bool clock_polarity;         // Clock polarity for SPI mode (true for high, false for low)
    bool clock_phase;            // Clock phase for SPI mode (true for leading edge, false for trailing edge)
    bool chip_select_idle;         // Chip select active (0=Active High, 1=Active Low) for SPI and 3-wire modes
    uint8_t submode;             // Submode for LED and INFRARED modes (e.g., "RGB", "IR TX", "IR RX")
    uint8_t tx_modulation;       // TX modulation for INFRARED mode (true for enabled, false for disabled)
    uint8_t rx_sensor;           // RX sensor for INFRARED mode (true for enabled, false for disabled)
} bpio_mode_configuration_t;

#endif
