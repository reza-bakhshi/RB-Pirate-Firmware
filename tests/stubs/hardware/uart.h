#ifndef _HARDWARE_UART_H
#define _HARDWARE_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t rsr;
} uart_hw_t;

typedef struct uart_inst {
    uart_hw_t hw;
} uart_inst_t;

extern uart_inst_t s_mock_uart0;
extern uart_inst_t s_mock_uart1;

#define uart0 (&s_mock_uart0)
#define uart1 (&s_mock_uart1)

#define UART_PARITY_NONE 0

static inline void uart_init(uart_inst_t *uart, uint32_t baudrate) { (void)uart; (void)baudrate; }
static inline void uart_deinit(uart_inst_t *uart) { (void)uart; }
static inline void uart_set_format(uart_inst_t *uart, unsigned int data_bits, unsigned int stop_bits, unsigned int parity) {
    (void)uart; (void)data_bits; (void)stop_bits; (void)parity;
}
static inline void uart_set_fifo_enabled(uart_inst_t *uart, bool enabled) { (void)uart; (void)enabled; }
static inline void uart_set_hw_flow(uart_inst_t *uart, bool cts, bool rts) { (void)uart; (void)cts; (void)rts; }
static inline void uart_write_blocking(uart_inst_t *uart, const uint8_t *src, size_t len) {
    (void)uart; (void)src; (void)len;
}
static inline void uart_tx_wait_blocking(uart_inst_t *uart) { (void)uart; }
static inline uart_hw_t *uart_get_hw(uart_inst_t *uart) { return &uart->hw; }

#ifdef BP_CUSTOM_UART_MOCKS
bool uart_is_readable(uart_inst_t *uart);
uint8_t uart_getc(uart_inst_t *uart);
void uart_putc_raw(uart_inst_t *uart, uint8_t c);
#else
static inline bool uart_is_readable(uart_inst_t *uart) { (void)uart; return false; }
static inline uint8_t uart_getc(uart_inst_t *uart) { (void)uart; return 0; }
static inline void uart_putc_raw(uart_inst_t *uart, uint8_t c) { (void)uart; (void)c; }
#endif

#endif
