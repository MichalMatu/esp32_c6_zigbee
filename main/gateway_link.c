#include "gateway_link.h"

#include "gateway_uart_link.h"

esp_err_t gateway_link_start(void)
{
    return gateway_uart_link_start();
}

void gateway_link_publish_event(const gateway_event_t *event)
{
    gateway_uart_link_publish_event(event);
}

uint32_t gateway_link_take_dropped(void)
{
    return gateway_uart_link_take_dropped();
}
