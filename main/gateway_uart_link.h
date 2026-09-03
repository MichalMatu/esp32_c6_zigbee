#pragma once

#include "gateway_link_backend.h"

#define GATEWAY_UART_LINK_TX_GPIO 18
#define GATEWAY_UART_LINK_RX_GPIO 19
#define GATEWAY_UART_LINK_BAUD_RATE 460800

const gateway_link_backend_t *gateway_uart_link_backend(void);
