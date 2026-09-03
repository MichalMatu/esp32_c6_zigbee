#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gateway_events.h"

typedef struct {
    const char *backend;
    bool peer_ready;
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t rx_invalid_frames;
    uint32_t queue_dropped;
    uint32_t short_writes;
    uint32_t tx_queue_depth;
    uint32_t tx_queue_high_water;
    uint32_t tx_queue_capacity;
    uint32_t last_tx_ms;
    uint32_t last_rx_ms;
    uint32_t minimum_free_heap_bytes;
    uint32_t tx_stack_high_water;
    uint32_t rx_stack_high_water;
} gateway_link_status_t;

esp_err_t gateway_link_start(void);
void gateway_link_publish_event(const gateway_event_t *event);
uint32_t gateway_link_take_dropped(void);
bool gateway_link_get_status(gateway_link_status_t *status);
