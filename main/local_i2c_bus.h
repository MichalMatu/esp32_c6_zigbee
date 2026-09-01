#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define LOCAL_I2C_SCL_GPIO 0
#define LOCAL_I2C_SDA_GPIO 1

esp_err_t local_i2c_bus_init(void);
esp_err_t local_i2c_bus_probe(uint16_t address, uint32_t timeout_ms);
esp_err_t local_i2c_bus_add_device(
    uint16_t address,
    uint32_t scl_speed_hz,
    i2c_master_dev_handle_t *device);
