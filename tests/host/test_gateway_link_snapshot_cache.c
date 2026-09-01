#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_snapshot_cache.h"

static gateway_link_input_descriptor_t descriptor(
    const char *id, uint8_t channel, bool available)
{
    gateway_link_input_descriptor_t value = {0};
    value.input.source = GATEWAY_SOURCE_LOCAL_I2C;
    value.input.channel = channel;
    strncpy(value.input.id, id, sizeof(value.input.id) - 1U);
    value.available = available;
    value.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_CO2;
    strcpy(value.model, "sensor-model");
    return value;
}

static void test_update_and_unavailable_preserves_descriptor(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);

    gateway_link_input_descriptor_t value = descriptor("sensor:a", 0U, true);
    assert(gateway_link_snapshot_cache_update(&cache, &value));
    assert(gateway_link_snapshot_cache_count(&cache) == 1U);

    gateway_link_input_descriptor_t copied = {0};
    assert(gateway_link_snapshot_cache_copy_slot(&cache, 0U, &copied));
    assert(copied.available);
    assert(strcmp(copied.input.id, "sensor:a") == 0);
    assert(strcmp(copied.model, "sensor-model") == 0);

    value.available = false;
    value.model[0] = '\0';
    assert(gateway_link_snapshot_cache_update(&cache, &value));
    assert(gateway_link_snapshot_cache_count(&cache) == 1U);
    assert(gateway_link_snapshot_cache_copy_slot(&cache, 0U, &copied));
    assert(!copied.available);
    assert(strcmp(copied.model, "sensor-model") == 0);
}

static void test_capacity_is_bounded(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "sensor:%u", (unsigned)i);
        gateway_link_input_descriptor_t value = descriptor(id, (uint8_t)i, true);
        assert(gateway_link_snapshot_cache_update(&cache, &value));
    }
    assert(gateway_link_snapshot_cache_count(&cache) == GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY);
    gateway_link_input_descriptor_t extra = descriptor("sensor:overflow", 0U, true);
    assert(!gateway_link_snapshot_cache_update(&cache, &extra));
}

static void test_rejects_invalid_input(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);
    gateway_link_input_descriptor_t value = {0};
    assert(!gateway_link_snapshot_cache_update(&cache, &value));
    assert(gateway_link_snapshot_cache_count(&cache) == 0U);
}

int main(void)
{
    test_update_and_unavailable_preserves_descriptor();
    test_capacity_is_bounded();
    test_rejects_invalid_input();
    puts("gateway_link_snapshot_cache host tests passed");
    return 0;
}
