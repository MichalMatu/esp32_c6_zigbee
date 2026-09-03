#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gateway_events.h"

esp_err_t gateway_link_start(void);
void gateway_link_publish_event(const gateway_event_t *event);
uint32_t gateway_link_take_dropped(void);
