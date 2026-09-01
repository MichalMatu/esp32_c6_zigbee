#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gateway_events.h"

#define GATEWAY_UART_LINK_TX_GPIO 18
#define GATEWAY_UART_LINK_RX_GPIO 19
#define GATEWAY_UART_LINK_BAUD_RATE 460800

esp_err_t gateway_uart_link_start(void);
void gateway_uart_link_publish_event(const gateway_event_t *event);
uint32_t gateway_uart_link_take_dropped(void);
