from pathlib import Path

ROOT = Path('.')

cache_h = r'''#pragma once

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
'''

cache_c = r'''#include "gateway_link_snapshot_cache.h"

#include <string.h>

static bool same_input(const gateway_input_id_t *a, const gateway_input_id_t *b)
{
    return a != NULL && b != NULL &&
        a->source == b->source &&
        a->channel == b->channel &&
        strcmp(a->id, b->id) == 0;
}

void gateway_link_snapshot_cache_init(gateway_link_snapshot_cache_t *cache)
{
    if (cache != NULL) {
        memset(cache, 0, sizeof(*cache));
    }
}

bool gateway_link_snapshot_cache_update(
    gateway_link_snapshot_cache_t *cache,
    const gateway_link_input_descriptor_t *descriptor)
{
    if (cache == NULL || descriptor == NULL || descriptor->input.id[0] == '\0') {
        return false;
    }

    gateway_link_snapshot_entry_t *target = NULL;
    gateway_link_snapshot_entry_t *free_entry = NULL;
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        gateway_link_snapshot_entry_t *entry = &cache->entries[i];
        if (entry->in_use && same_input(&entry->descriptor.input, &descriptor->input)) {
            target = entry;
            break;
        }
        if (!entry->in_use && free_entry == NULL) {
            free_entry = entry;
        }
    }
    if (target == NULL) {
        target = free_entry;
    }
    if (target == NULL) {
        return false;
    }

    if (!target->in_use) {
        memset(target, 0, sizeof(*target));
        target->in_use = true;
        target->descriptor.input = descriptor->input;
    }
    target->descriptor.available = descriptor->available;
    if (descriptor->capabilities != 0U || target->descriptor.capabilities == 0U) {
        target->descriptor.capabilities = descriptor->capabilities;
    }
    if (descriptor->model[0] != '\0') {
        strncpy(target->descriptor.model,
                descriptor->model,
                sizeof(target->descriptor.model) - 1U);
        target->descriptor.model[sizeof(target->descriptor.model) - 1U] = '\0';
    }
    return true;
}

bool gateway_link_snapshot_cache_copy_slot(
    const gateway_link_snapshot_cache_t *cache,
    size_t slot,
    gateway_link_input_descriptor_t *descriptor)
{
    if (cache == NULL || descriptor == NULL ||
        slot >= GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY ||
        !cache->entries[slot].in_use) {
        return false;
    }
    *descriptor = cache->entries[slot].descriptor;
    return true;
}

size_t gateway_link_snapshot_cache_count(const gateway_link_snapshot_cache_t *cache)
{
    if (cache == NULL) {
        return 0U;
    }
    size_t count = 0U;
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        if (cache->entries[i].in_use) {
            ++count;
        }
    }
    return count;
}
'''

test_cache = r'''#include <assert.h>
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
'''

(ROOT / 'main/gateway_link_snapshot_cache.h').write_text(cache_h)
(ROOT / 'main/gateway_link_snapshot_cache.c').write_text(cache_c)
(ROOT / 'tests/host/test_gateway_link_snapshot_cache.c').write_text(test_cache)

p = ROOT / 'main/gateway_uart_link.c'
s = p.read_text()
s = s.replace('static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;\n', '')
old = '''void gateway_uart_link_publish_event(const gateway_event_t *event)\n{\n    if (event != NULL &&\n        (event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ||\n         event->kind == GATEWAY_EVENT_INPUT_UNAVAILABLE)) {\n        portENTER_CRITICAL(&s_snapshot_lock);\n        const bool cached = gateway_link_snapshot_cache_update_event(&s_snapshot_cache, event);\n        portEXIT_CRITICAL(&s_snapshot_lock);\n        if (!cached) {\n            note_drop();\n        }\n    }\n    gateway_link_message_t message;\n    if (gateway_link_message_from_event(event, &message)) {\n        (void)enqueue_message(&message);\n    }\n}\n'''
new = '''void gateway_uart_link_publish_event(const gateway_event_t *event)\n{\n    gateway_link_message_t message;\n    if (gateway_link_message_from_event(event, &message)) {\n        (void)enqueue_message(&message);\n    }\n}\n'''
assert old in s
s = s.replace(old, new, 1)
old = '''        gateway_link_input_descriptor_t descriptor;\n        portENTER_CRITICAL(&s_snapshot_lock);\n        const bool present = gateway_link_snapshot_cache_copy_slot(\n            &s_snapshot_cache, slot, &descriptor);\n        portEXIT_CRITICAL(&s_snapshot_lock);\n'''
new = '''        gateway_link_input_descriptor_t descriptor;\n        const bool present = gateway_link_snapshot_cache_copy_slot(\n            &s_snapshot_cache, slot, &descriptor);\n'''
assert old in s
s = s.replace(old, new, 1)
old = '''        if (item.kind == LINK_TX_ITEM_SNAPSHOT) {\n            transmit_snapshot(item.snapshot_token);\n        } else {\n            write_message(&item.message, item.sequence);\n        }\n'''
new = '''        if (item.kind == LINK_TX_ITEM_SNAPSHOT) {\n            transmit_snapshot(item.snapshot_token);\n        } else {\n            if (item.message.type == GATEWAY_LINK_MSG_INPUT_DESCRIPTOR) {\n                gateway_link_input_descriptor_t descriptor;\n                if (gateway_link_decode_input_descriptor_payload(\n                        item.message.payload, item.message.payload_length,\n                        &descriptor) != GATEWAY_LINK_OK ||\n                    !gateway_link_snapshot_cache_update(&s_snapshot_cache, &descriptor)) {\n                    note_drop();\n                }\n            }\n            write_message(&item.message, item.sequence);\n        }\n'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)

p = ROOT / '.github/workflows/quality.yml'
s = p.read_text()
s = s.replace('            main/gateway_link_snapshot_cache.c \\\n            main/gateway_inputs.c \\\n',
              '            main/gateway_link_snapshot_cache.c \\\n', 1)
p.write_text(s)

p = ROOT / 'docs/ARCHITECTURE.md'
s = p.read_text()
s = s.replace('`gateway_link_snapshot_cache.c/.h` owns a bounded cache of protocol-neutral input descriptors for reconnect resynchronization. It is transport state only, not the application input registry; incremental events remain authoritative after a snapshot.',
              '`gateway_link_snapshot_cache.c/.h` owns a bounded cache of protocol-neutral input descriptors for reconnect resynchronization. The UART TX task is its sole runtime owner, so descriptor caching and snapshot replay have deterministic ordering without a cross-task lock. It is transport state only, not the application input registry; incremental events remain authoritative after a snapshot.')
p.write_text(s)
