#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gateway_link_protocol.h"

#define GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY 64U

typedef struct {
    bool in_use;
    gateway_link_input_descriptor_t descriptor;
} gateway_link_snapshot_entry_t;

typedef struct {
    gateway_link_snapshot_entry_t entries[GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY];
} gateway_link_snapshot_cache_t;

void gateway_link_snapshot_cache_init(gateway_link_snapshot_cache_t *cache);

bool gateway_link_snapshot_cache_update(
    gateway_link_snapshot_cache_t *cache,
    const gateway_link_input_descriptor_t *descriptor);

bool gateway_link_snapshot_cache_copy_slot(
    const gateway_link_snapshot_cache_t *cache,
    size_t slot,
    gateway_link_input_descriptor_t *descriptor);

size_t gateway_link_snapshot_cache_count(const gateway_link_snapshot_cache_t *cache);
