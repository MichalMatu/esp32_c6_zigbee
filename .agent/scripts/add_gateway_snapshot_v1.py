from pathlib import Path

ROOT = Path('.')

cache_h = r'''#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_events.h"
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

bool gateway_link_snapshot_cache_update_event(
    gateway_link_snapshot_cache_t *cache,
    const gateway_event_t *event);

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

bool gateway_link_snapshot_cache_update_event(
    gateway_link_snapshot_cache_t *cache,
    const gateway_event_t *event)
{
    if (cache == NULL || event == NULL ||
        (event->kind != GATEWAY_EVENT_INPUT_AVAILABLE &&
         event->kind != GATEWAY_EVENT_INPUT_UNAVAILABLE) ||
        event->input.id[0] == '\0') {
        return false;
    }

    gateway_link_snapshot_entry_t *target = NULL;
    gateway_link_snapshot_entry_t *free_entry = NULL;
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        gateway_link_snapshot_entry_t *entry = &cache->entries[i];
        if (entry->in_use && same_input(&entry->descriptor.input, &event->input)) {
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
        target->descriptor.input = event->input;
    }
    target->descriptor.available = event->kind == GATEWAY_EVENT_INPUT_AVAILABLE;
    if (event->data.input_desc.capabilities != 0U ||
        target->descriptor.capabilities == 0U) {
        target->descriptor.capabilities = event->data.input_desc.capabilities;
    }
    if (event->data.input_desc.model[0] != '\0') {
        strncpy(target->descriptor.model,
                event->data.input_desc.model,
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

static gateway_event_t descriptor_event(const char *id, uint8_t channel, bool available)
{
    gateway_event_t event = {0};
    event.kind = available ? GATEWAY_EVENT_INPUT_AVAILABLE : GATEWAY_EVENT_INPUT_UNAVAILABLE;
    event.input = gateway_input_make(GATEWAY_SOURCE_LOCAL_I2C, id, channel);
    event.data.input_desc.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE | GATEWAY_INPUT_CAP_CO2;
    strcpy(event.data.input_desc.model, "sensor-model");
    return event;
}

static void test_update_and_unavailable_preserves_descriptor(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);

    gateway_event_t event = descriptor_event("sensor:a", 0U, true);
    assert(gateway_link_snapshot_cache_update_event(&cache, &event));
    assert(gateway_link_snapshot_cache_count(&cache) == 1U);

    gateway_link_input_descriptor_t descriptor = {0};
    assert(gateway_link_snapshot_cache_copy_slot(&cache, 0U, &descriptor));
    assert(descriptor.available);
    assert(strcmp(descriptor.input.id, "sensor:a") == 0);
    assert(strcmp(descriptor.model, "sensor-model") == 0);

    event.kind = GATEWAY_EVENT_INPUT_UNAVAILABLE;
    event.data.input_desc.model[0] = '\0';
    assert(gateway_link_snapshot_cache_update_event(&cache, &event));
    assert(gateway_link_snapshot_cache_count(&cache) == 1U);
    assert(gateway_link_snapshot_cache_copy_slot(&cache, 0U, &descriptor));
    assert(!descriptor.available);
    assert(strcmp(descriptor.model, "sensor-model") == 0);
}

static void test_distinct_source_channel_and_capacity(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        char id[24];
        snprintf(id, sizeof(id), "sensor:%u", (unsigned)i);
        gateway_event_t event = descriptor_event(id, (uint8_t)(i & 0xffU), true);
        assert(gateway_link_snapshot_cache_update_event(&cache, &event));
    }
    assert(gateway_link_snapshot_cache_count(&cache) == GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY);
    gateway_event_t extra = descriptor_event("sensor:overflow", 0U, true);
    assert(!gateway_link_snapshot_cache_update_event(&cache, &extra));
}

static void test_rejects_non_descriptor_event(void)
{
    gateway_link_snapshot_cache_t cache;
    gateway_link_snapshot_cache_init(&cache);
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_MEASUREMENT;
    assert(!gateway_link_snapshot_cache_update_event(&cache, &event));
    assert(gateway_link_snapshot_cache_count(&cache) == 0U);
}

int main(void)
{
    test_update_and_unavailable_preserves_descriptor();
    test_distinct_source_channel_and_capacity();
    test_rejects_non_descriptor_event();
    puts("gateway_link_snapshot_cache host tests passed");
    return 0;
}
'''

(ROOT / 'main/gateway_link_snapshot_cache.h').write_text(cache_h)
(ROOT / 'main/gateway_link_snapshot_cache.c').write_text(cache_c)
(ROOT / 'tests/host/test_gateway_link_snapshot_cache.c').write_text(test_cache)

# Control parsing: SNAPSHOT_REQUEST is a real action and snapshot feature is now truthful.
h = ROOT / 'main/gateway_link_control.h'
s = h.read_text()
s = s.replace('    GATEWAY_LINK_CONTROL_PING,\n', '    GATEWAY_LINK_CONTROL_PING,\n    GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST,\n', 1)
needle = 'bool gateway_link_make_pong_message(uint32_t token, gateway_link_message_t *message);\n'
assert needle in s
s = s.replace(needle, needle + 'bool gateway_link_make_snapshot_marker_message(\n    uint8_t type, uint32_t token, gateway_link_message_t *message);\n', 1)
h.write_text(s)

c = ROOT / 'main/gateway_link_control.c'
s = c.read_text()
s = s.replace('.features = GATEWAY_LINK_FEATURE_PERMIT_JOIN,',
              '.features = GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN,', 1)
needle = '''    case GATEWAY_LINK_MSG_PING:\n        if (gateway_link_decode_u32_payload(\n                frame->payload, frame->payload_length, &action.token) != GATEWAY_LINK_OK) {\n            action.kind = GATEWAY_LINK_CONTROL_INVALID;\n            return action;\n        }\n        action.kind = GATEWAY_LINK_CONTROL_PING;\n        return action;\n\n'''
assert needle in s
replacement = needle + '''    case GATEWAY_LINK_MSG_SNAPSHOT_REQUEST:\n        if (gateway_link_decode_u32_payload(\n                frame->payload, frame->payload_length, &action.token) != GATEWAY_LINK_OK) {\n            action.kind = GATEWAY_LINK_CONTROL_INVALID;\n            return action;\n        }\n        action.kind = GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST;\n        return action;\n\n'''
s = s.replace(needle, replacement, 1)
append = r'''

bool gateway_link_make_snapshot_marker_message(
    uint8_t type, uint32_t token, gateway_link_message_t *message)
{
    if (message == NULL ||
        (type != GATEWAY_LINK_MSG_SNAPSHOT_BEGIN &&
         type != GATEWAY_LINK_MSG_SNAPSHOT_END)) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = type;
    return gateway_link_encode_u32_payload(
        token, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}
'''
s = s.rstrip() + append
c.write_text(s)

# Extend control host tests with snapshot semantics and truthful feature bits.
p = ROOT / 'tests/host/test_gateway_link_control.c'
s = p.read_text()
s = s.replace('assert(hello.features == GATEWAY_LINK_FEATURE_PERMIT_JOIN);',
              'assert(hello.features == (GATEWAY_LINK_FEATURE_SNAPSHOT | GATEWAY_LINK_FEATURE_PERMIT_JOIN));')
needle = 'static void test_measurement_policy_truthfully_unsupported(void)\n'
assert needle in s
snapshot_test = r'''static void test_snapshot_request_and_markers(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_SNAPSHOT_REQUEST};
    assert(gateway_link_encode_u32_payload(
        0x1234abcdUL, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST);
    assert(action.token == 0x1234abcdUL);

    gateway_link_message_t message;
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_BEGIN, action.token, &message));
    uint32_t token = 0U;
    assert(gateway_link_decode_u32_payload(
        message.payload, message.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == action.token);
    assert(gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_SNAPSHOT_END, action.token, &message));
    assert(!gateway_link_make_snapshot_marker_message(
        GATEWAY_LINK_MSG_PING, action.token, &message));
}

'''
s = s.replace(needle, snapshot_test + needle, 1)
s = s.replace('    test_ping_and_pong();\n', '    test_ping_and_pong();\n    test_snapshot_request_and_markers();\n', 1)
p.write_text(s)

# Runtime: cache descriptors at the normalized event boundary and execute snapshots in TX task.
p = ROOT / 'main/gateway_uart_link.c'
s = p.read_text()
s = s.replace('#include "gateway_link_protocol.h"\n', '#include "gateway_link_protocol.h"\n#include "gateway_link_snapshot_cache.h"\n', 1)
old = '''typedef struct {\n    uint32_t sequence;\n    gateway_link_message_t message;\n} tx_item_t;\n'''
new = '''typedef enum {\n    LINK_TX_ITEM_MESSAGE = 0,\n    LINK_TX_ITEM_SNAPSHOT,\n} tx_item_kind_t;\n\ntypedef struct {\n    tx_item_kind_t kind;\n    uint32_t sequence;\n    uint32_t snapshot_token;\n    gateway_link_message_t message;\n} tx_item_t;\n'''
assert old in s
s = s.replace(old, new, 1)
needle = 'static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;\n'
assert needle in s
s = s.replace(needle, needle + 'static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;\nstatic gateway_link_snapshot_cache_t s_snapshot_cache;\n', 1)
old = '''    const tx_item_t item = {\n        .sequence = allocate_sequence(),\n        .message = *message,\n    };\n'''
new = '''    const tx_item_t item = {\n        .kind = LINK_TX_ITEM_MESSAGE,\n        .sequence = allocate_sequence(),\n        .message = *message,\n    };\n'''
assert old in s
s = s.replace(old, new, 1)
needle = '''    return true;\n}\n\nvoid gateway_uart_link_publish_event(const gateway_event_t *event)\n'''
assert needle in s
extra = r'''    return true;
}

static bool enqueue_snapshot(uint32_t token)
{
    if (s_tx_queue == NULL) {
        return false;
    }
    const tx_item_t item = {
        .kind = LINK_TX_ITEM_SNAPSHOT,
        .snapshot_token = token,
    };
    if (xQueueSend(s_tx_queue, &item, 0U) != pdPASS) {
        note_drop();
        return false;
    }
    return true;
}

void gateway_uart_link_publish_event(const gateway_event_t *event)
'''
s = s.replace(needle, extra, 1)
old = '''void gateway_uart_link_publish_event(const gateway_event_t *event)\n{\n    gateway_link_message_t message;\n    if (gateway_link_message_from_event(event, &message)) {\n        (void)enqueue_message(&message);\n    }\n}\n'''
new = '''void gateway_uart_link_publish_event(const gateway_event_t *event)\n{\n    if (event != NULL &&\n        (event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ||\n         event->kind == GATEWAY_EVENT_INPUT_UNAVAILABLE)) {\n        portENTER_CRITICAL(&s_snapshot_lock);\n        const bool cached = gateway_link_snapshot_cache_update_event(&s_snapshot_cache, event);\n        portEXIT_CRITICAL(&s_snapshot_lock);\n        if (!cached) {\n            note_drop();\n        }\n    }\n    gateway_link_message_t message;\n    if (gateway_link_message_from_event(event, &message)) {\n        (void)enqueue_message(&message);\n    }\n}\n'''
assert old in s
s = s.replace(old, new, 1)
needle = '''    case GATEWAY_LINK_CONTROL_PING:\n        if (gateway_link_make_pong_message(action.token, &response)) {\n            (void)enqueue_message(&response);\n        }\n        break;\n'''
assert needle in s
s = s.replace(needle, needle + '''    case GATEWAY_LINK_CONTROL_SNAPSHOT_REQUEST:\n        if (!enqueue_snapshot(action.token)) {\n            ESP_LOGW(TAG, "failed to queue GatewayLink snapshot token=%lu",\n                     (unsigned long)action.token);\n        }\n        break;\n''', 1)

# Replace direct tx encoding block with reusable writer and snapshot executor.
needle = 'static void tx_task(void *arg)\n'
assert needle in s
helpers = r'''static void write_message(const gateway_link_message_t *message, uint32_t sequence)
{
    if (message == NULL) {
        return;
    }
    gateway_link_frame_t frame = {
        .type = message->type,
        .flags = message->flags,
        .sequence = sequence,
        .payload_length = message->payload_length,
    };
    if (frame.payload_length != 0U) {
        memcpy(frame.payload, message->payload, frame.payload_length);
    }
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    if (gateway_link_encode_frame(
            &frame, encoded, sizeof(encoded), &encoded_length) != GATEWAY_LINK_OK) {
        ESP_LOGW(TAG, "failed to encode GatewayLink frame type=0x%02x seq=%lu",
                 frame.type, (unsigned long)frame.sequence);
        return;
    }
    const int written = uart_write_bytes(LINK_UART, encoded, encoded_length);
    if (written != (int)encoded_length) {
        ESP_LOGW(TAG, "short UART write seq=%lu wrote=%d expected=%u",
                 (unsigned long)frame.sequence, written, (unsigned)encoded_length);
    }
}

static void transmit_snapshot(uint32_t token)
{
    gateway_link_message_t message;
    if (!gateway_link_make_snapshot_marker_message(
            GATEWAY_LINK_MSG_SNAPSHOT_BEGIN, token, &message)) {
        return;
    }
    write_message(&message, allocate_sequence());

    size_t sent = 0U;
    for (size_t slot = 0U; slot < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++slot) {
        gateway_link_input_descriptor_t descriptor;
        portENTER_CRITICAL(&s_snapshot_lock);
        const bool present = gateway_link_snapshot_cache_copy_slot(
            &s_snapshot_cache, slot, &descriptor);
        portEXIT_CRITICAL(&s_snapshot_lock);
        if (!present) {
            continue;
        }
        memset(&message, 0, sizeof(message));
        message.type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
        if (gateway_link_encode_input_descriptor_payload(
                &descriptor, message.payload, sizeof(message.payload),
                &message.payload_length) != GATEWAY_LINK_OK) {
            ESP_LOGW(TAG, "failed to encode snapshot descriptor slot=%u", (unsigned)slot);
            continue;
        }
        write_message(&message, allocate_sequence());
        ++sent;
    }

    if (gateway_link_make_snapshot_marker_message(
            GATEWAY_LINK_MSG_SNAPSHOT_END, token, &message)) {
        write_message(&message, allocate_sequence());
    }
    ESP_LOGI(TAG, "GatewayLink snapshot token=%lu descriptors=%u",
             (unsigned long)token, (unsigned)sent);
}

'''
s = s.replace(needle, helpers + needle, 1)
old = r'''static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    for (;;) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdPASS) {
            continue;
        }
        gateway_link_frame_t frame = {
            .type = item.message.type,
            .flags = item.message.flags,
            .sequence = item.sequence,
            .payload_length = item.message.payload_length,
        };
        if (frame.payload_length != 0U) {
            memcpy(frame.payload, item.message.payload, frame.payload_length);
        }
        size_t encoded_length = 0U;
        if (gateway_link_encode_frame(
                &frame, encoded, sizeof(encoded), &encoded_length) != GATEWAY_LINK_OK) {
            ESP_LOGW(TAG, "failed to encode GatewayLink frame type=0x%02x seq=%lu",
                     frame.type, (unsigned long)frame.sequence);
            continue;
        }
        const int written = uart_write_bytes(LINK_UART, encoded, encoded_length);
        if (written != (int)encoded_length) {
            ESP_LOGW(TAG, "short UART write seq=%lu wrote=%d expected=%u",
                     (unsigned long)frame.sequence, written, (unsigned)encoded_length);
        }
    }
}
'''
new = r'''static void tx_task(void *arg)
{
    (void)arg;
    tx_item_t item;
    for (;;) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (item.kind == LINK_TX_ITEM_SNAPSHOT) {
            transmit_snapshot(item.snapshot_token);
        } else {
            write_message(&item.message, item.sequence);
        }
    }
}
'''
assert old in s
s = s.replace(old, new, 1)
needle = '''    s_tx_queue = xQueueCreateStatic(\n'''
assert needle in s
s = s.replace(needle, '    gateway_link_snapshot_cache_init(&s_snapshot_cache);\n\n' + needle, 1)
p.write_text(s)

# Firmware source and CI host cache test.
p = ROOT / 'main/CMakeLists.txt'
s = p.read_text()
needle = '        "gateway_link_stream.c"\n'
assert needle in s
s = s.replace(needle, needle + '        "gateway_link_snapshot_cache.c"\n', 1)
p.write_text(s)

p = ROOT / '.github/workflows/quality.yml'
s = p.read_text()
marker = '  host-link-control:\n'
assert marker in s
job = '''  host-link-snapshot-cache:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build GatewayLink snapshot cache host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -Imain \\\n            tests/host/test_gateway_link_snapshot_cache.c \\\n            main/gateway_link_snapshot_cache.c \\\n            main/gateway_inputs.c \\\n            -o /tmp/test_gateway_link_snapshot_cache\n      - name: Run GatewayLink snapshot cache host tests\n        run: /tmp/test_gateway_link_snapshot_cache\n\n'''
s = s.replace(marker, job + marker, 1)
p.write_text(s)

# Docs: snapshot is now descriptor-resync implementation, still no measurement-policy feature.
p = ROOT / 'docs/ARCHITECTURE.md'
s = p.read_text()
needle = '- `gateway_link_stream.c/.h` is the pure incremental COBS frame stream decoder. Oversize/corrupt frames are dropped at the delimiter so later frames resynchronize without dynamic allocation.\n'
assert needle in s
s = s.replace(needle, needle + '- `gateway_link_snapshot_cache.c/.h` owns a bounded cache of protocol-neutral input descriptors for reconnect resynchronization. It is transport state only, not the application input registry; incremental events remain authoritative after a snapshot.\n', 1)
p.write_text(s)

p = ROOT / 'docs/GATEWAY_LINK_V1.md'
s = p.read_text()
s = s.replace('Snapshot and measurement-policy application are not advertised until their backing state/policy layers exist.',
              'Snapshot is implemented as bounded descriptor replay; measurement-policy application is not advertised until its backing policy layer exists.')
s = s.replace('The C6 currently advertises only the `permit-join` feature bit. Snapshot and measurement-policy wire types are reserved by v1 but are not advertised until their runtime implementations exist.',
              'The C6 currently advertises `snapshot` and `permit-join`. Measurement-policy remains reserved by v1 but is not advertised until its runtime implementation exists.')
needle = 'After either MCU reconnects, the S3 requests a snapshot. The C6 sends descriptors for all currently known stable inputs between `SNAPSHOT_BEGIN` and `SNAPSHOT_END`, then continues with incremental descriptors and measurements. A lost frame is detected by sequence gaps and/or CRC; COBS provides delimiter-level resynchronization.\n'
assert needle in s
s = s.replace(needle, needle + '\nThe C6 snapshot cache has a fixed capacity of 64 descriptors. Snapshot execution runs inside the UART TX task rather than enqueueing every descriptor into the normal 16-item TX queue, so a legitimate snapshot cannot self-overflow that queue. Cache exhaustion is counted as a visible link drop; no unbounded allocation is used.\n', 1)
p.write_text(s)

p = ROOT / 'README.md'
s = p.read_text()
old = 'Snapshot and source-neutral measurement-policy application remain disabled until their real backing state/policy layers are implemented.'
new = 'Descriptor snapshot/resync is implemented with a bounded transport cache and TX-task replay; source-neutral measurement-policy application remains disabled until its real backing policy layer is implemented.'
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)

for path in [
    ROOT / 'main/gateway_link_snapshot_cache.h',
    ROOT / 'main/gateway_link_snapshot_cache.c',
    ROOT / 'tests/host/test_gateway_link_snapshot_cache.c',
]:
    assert b'\x00' not in path.read_bytes()
