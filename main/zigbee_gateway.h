#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t zigbee_gateway_start(void);
esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds);
