// minimal host stub of pirate.h for tests
#ifndef HOST_PIRATE_H
#define HOST_PIRATE_H
#include <stdint.h>
#define count_of(a) (sizeof(a) / sizeof((a)[0]))

#ifndef BP_FLASH_DISK_BLOCK_SIZE
#define BP_FLASH_DISK_BLOCK_SIZE 512
#endif

#ifndef M_SPI_PORT
#define M_SPI_PORT spi1
#endif

#ifndef M_UART_PORT
#define M_UART_PORT uart0
#endif

#define BP_DEBUG_LEVEL_ERROR 0
#define BP_DEBUG_DEFAULT_CATEGORY 0
#ifndef BP_DEBUG_PRINT
#define BP_DEBUG_PRINT(...) do {} while(0)
#endif

#endif
