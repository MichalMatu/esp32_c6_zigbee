#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    const char *name;
    esp_err_t (*start)(void);
    void (*stop)(void);
    int (*read)(uint8_t *buffer, size_t capacity, uint32_t timeout_ms);
    int (*write)(const uint8_t *buffer, size_t length);
} gateway_link_backend_t;
