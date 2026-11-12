/**
 * @file cmsis_dap.h
 * @brief ARM CMSIS-DAP v2 Serial Wire Debug (SWD) probe implementation.
 * @details Implements the ARM CMSIS-DAP v2 protocol over USB bulk endpoints
 *          (WinUSB) and binary mode for OpenOCD, pyOCD, and Keil debuggers.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CMSIS-DAP Command IDs
#define DAP_ID_INFO                     0x00
#define DAP_ID_HOST_STATUS              0x01
#define DAP_ID_CONNECT                  0x02
#define DAP_ID_DISCONNECT               0x03
#define DAP_ID_TRANSFER_CONFIGURE       0x04
#define DAP_ID_TRANSFER                 0x05
#define DAP_ID_TRANSFER_BLOCK           0x06
#define DAP_ID_TRANSFER_ABORT           0x07
#define DAP_ID_WRITE_ABORT              0x08
#define DAP_ID_DELAY                    0x09
#define DAP_ID_RESET_TARGET             0x0A
#define DAP_ID_SWJ_PINS                 0x10
#define DAP_ID_SWJ_CLOCK                0x11
#define DAP_ID_SWJ_SEQUENCE             0x12
#define DAP_ID_SWD_CONFIGURE            0x13
#define DAP_ID_SWD_SEQUENCE             0x1D
#define DAP_ID_QUEUE_COMMANDS           0x7E
#define DAP_ID_EXECUTE_COMMANDS         0x7F

// DAP Status Responses
#define DAP_OK                          0x00
#define DAP_ERROR                       0xFF

// DAP Port Selection
#define DAP_PORT_DEFAULT                0x00
#define DAP_PORT_SWD                    0x01
#define DAP_PORT_JTAG                   0x02

// DAP Transfer ACK responses
#define DAP_TRANSFER_OK                 1
#define DAP_TRANSFER_WAIT               2
#define DAP_TRANSFER_FAULT              4
#define DAP_TRANSFER_NO_ACK             7
#define DAP_TRANSFER_ERROR              8
#define DAP_TRANSFER_MISMATCH           16

// DAP Info IDs
#define DAP_INFO_VENDOR                 0x01
#define DAP_INFO_PRODUCT                0x02
#define DAP_INFO_SERNUM                 0x03
#define DAP_INFO_FW_VER                 0x04
#define DAP_INFO_DEVICE_VENDOR          0x05
#define DAP_INFO_DEVICE_NAME            0x06
#define DAP_INFO_BOARD_VENDOR           0x09
#define DAP_INFO_BOARD_NAME             0x0A
#define DAP_INFO_PRODUCT_FIRMWARE_VER   0x0B
#define DAP_INFO_CAPABILITIES           0xF0
#define DAP_INFO_TEST_DOMAIN_TIMER      0xF1
#define DAP_INFO_UART_BUF_SIZE          0xFD
#define DAP_INFO_PACKET_COUNT           0xFE
#define DAP_INFO_PACKET_SIZE            0xFF

#define CMSIS_DAP_PACKET_SIZE           64
#define CMSIS_DAP_MAX_PACKET_SIZE       128

/**
 * @brief Initialize the CMSIS-DAP subsystem.
 */
void cmsis_dap_init(void);

/**
 * @brief Setup CMSIS-DAP binary mode and configure SWD pins.
 */
void cmsis_dap_setup(void);

/**
 * @brief Main polling service for CMSIS-DAP over USB vendor bulk endpoints or binmode.
 */
void cmsis_dap_service(void);

/**
 * @brief Cleanup CMSIS-DAP and return SWD pins to high-impedance mode.
 */
void cmsis_dap_cleanup(void);

/**
 * @brief Process an incoming CMSIS-DAP packet and generate response.
 * @param req Pointer to request buffer
 * @param req_len Length of request in bytes
 * @param resp Pointer to response output buffer
 * @param max_resp_len Maximum size of response buffer
 * @return Number of response bytes generated
 */
uint32_t cmsis_dap_process_command(const uint8_t *req, uint32_t req_len,
                                   uint8_t *resp, uint32_t max_resp_len);

extern const char cmsis_dap_name[];

/**
 * @brief Check if CMSIS-DAP is currently connected to a target.
 */
bool cmsis_dap_is_connected(void);

/**
 * @brief Get human-readable binmode name.
 */
const char* cmsis_dap_mode_name(void);

#ifdef __cplusplus
}
#endif
