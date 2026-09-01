from pathlib import Path

ROOT = Path('.')

stream_h = r'''#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_link_protocol.h"

typedef enum {
    GATEWAY_LINK_STREAM_NONE = 0,
    GATEWAY_LINK_STREAM_FRAME,
    GATEWAY_LINK_STREAM_DROPPED,
} gateway_link_stream_event_t;

typedef struct {
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t length;
    bool overflow;
} gateway_link_stream_decoder_t;

void gateway_link_stream_init(gateway_link_stream_decoder_t *decoder);

gateway_link_stream_event_t gateway_link_stream_feed(
    gateway_link_stream_decoder_t *decoder,
    uint8_t byte,
    gateway_link_frame_t *frame,
    gateway_link_result_t *decode_result);
'''

stream_c = r'''#include "gateway_link_stream.h"

#include <string.h>

void gateway_link_stream_init(gateway_link_stream_decoder_t *decoder)
{
    if (decoder != NULL) {
        memset(decoder, 0, sizeof(*decoder));
    }
}

gateway_link_stream_event_t gateway_link_stream_feed(
    gateway_link_stream_decoder_t *decoder,
    uint8_t byte,
    gateway_link_frame_t *frame,
    gateway_link_result_t *decode_result)
{
    if (decoder == NULL || frame == NULL || decode_result == NULL) {
        return GATEWAY_LINK_STREAM_DROPPED;
    }

    if (byte != 0U) {
        if (!decoder->overflow) {
            if (decoder->length < sizeof(decoder->encoded)) {
                decoder->encoded[decoder->length++] = byte;
            } else {
                decoder->overflow = true;
            }
        }
        return GATEWAY_LINK_STREAM_NONE;
    }

    if (decoder->overflow) {
        decoder->length = 0U;
        decoder->overflow = false;
        *decode_result = GATEWAY_LINK_BUFFER_TOO_SMALL;
        return GATEWAY_LINK_STREAM_DROPPED;
    }
    if (decoder->length == 0U) {
        return GATEWAY_LINK_STREAM_NONE;
    }

    *decode_result = gateway_link_decode_frame(decoder->encoded, decoder->length, frame);
    decoder->length = 0U;
    if (*decode_result != GATEWAY_LINK_OK) {
        return GATEWAY_LINK_STREAM_DROPPED;
    }
    return GATEWAY_LINK_STREAM_FRAME;
}
'''

control_h = r'''#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"

typedef enum {
    GATEWAY_LINK_CONTROL_IGNORE = 0,
    GATEWAY_LINK_CONTROL_HELLO,
    GATEWAY_LINK_CONTROL_HELLO_ACK,
    GATEWAY_LINK_CONTROL_PING,
    GATEWAY_LINK_CONTROL_PERMIT_JOIN,
    GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED,
    GATEWAY_LINK_CONTROL_INVALID,
} gateway_link_control_kind_t;

typedef struct {
    gateway_link_control_kind_t kind;
    uint32_t request_id;
    uint32_t token;
    uint8_t permit_join_seconds;
    gateway_link_hello_t peer_hello;
} gateway_link_control_action_t;

gateway_link_control_action_t gateway_link_control_parse(
    const gateway_link_frame_t *frame);

bool gateway_link_control_peer_compatible(const gateway_link_hello_t *peer);

bool gateway_link_make_hello_ack_message(gateway_link_message_t *message);
bool gateway_link_make_pong_message(uint32_t token, gateway_link_message_t *message);
bool gateway_link_make_config_result_message(
    uint32_t request_id,
    gateway_link_config_status_t status,
    gateway_link_message_t *message);
'''

control_c = r'''#include "gateway_link_control.h"

#include <string.h>

gateway_link_control_action_t gateway_link_control_parse(
    const gateway_link_frame_t *frame)
{
    gateway_link_control_action_t action = {0};
    if (frame == NULL) {
        action.kind = GATEWAY_LINK_CONTROL_INVALID;
        return action;
    }

    switch (frame->type) {
    case GATEWAY_LINK_MSG_HELLO:
    case GATEWAY_LINK_MSG_HELLO_ACK:
        if (gateway_link_decode_hello_payload(
                frame->payload, frame->payload_length, &action.peer_hello) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = frame->type == GATEWAY_LINK_MSG_HELLO
            ? GATEWAY_LINK_CONTROL_HELLO
            : GATEWAY_LINK_CONTROL_HELLO_ACK;
        return action;

    case GATEWAY_LINK_MSG_PING:
        if (gateway_link_decode_u32_payload(
                frame->payload, frame->payload_length, &action.token) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_PING;
        return action;

    case GATEWAY_LINK_MSG_PERMIT_JOIN:
    {
        gateway_link_permit_join_t command = {0};
        if (gateway_link_decode_permit_join_payload(
                frame->payload, frame->payload_length, &command) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_PERMIT_JOIN;
        action.request_id = command.request_id;
        action.permit_join_seconds = command.duration_seconds;
        return action;
    }

    case GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY:
    {
        gateway_link_measurement_policy_t policy = {0};
        if (gateway_link_decode_measurement_policy_payload(
                frame->payload, frame->payload_length, &policy) != GATEWAY_LINK_OK) {
            action.kind = GATEWAY_LINK_CONTROL_INVALID;
            return action;
        }
        action.kind = GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED;
        action.request_id = policy.request_id;
        return action;
    }

    default:
        action.kind = GATEWAY_LINK_CONTROL_IGNORE;
        return action;
    }
}

bool gateway_link_control_peer_compatible(const gateway_link_hello_t *peer)
{
    if (peer == NULL || peer->role != GATEWAY_LINK_ROLE_S3_HOST) {
        return false;
    }
    return peer->min_version <= GATEWAY_LINK_PROTOCOL_VERSION &&
        peer->max_version >= GATEWAY_LINK_PROTOCOL_VERSION &&
        peer->max_frame_bytes >= GATEWAY_LINK_MAX_FRAME_BYTES;
}

bool gateway_link_make_hello_ack_message(gateway_link_message_t *message)
{
    if (message == NULL) return false;
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_HELLO_ACK;
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = GATEWAY_LINK_FEATURE_PERMIT_JOIN,
    };
    return gateway_link_encode_hello_payload(
        &hello, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_make_pong_message(uint32_t token, gateway_link_message_t *message)
{
    if (message == NULL) return false;
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_PONG;
    return gateway_link_encode_u32_payload(
        token, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_make_config_result_message(
    uint32_t request_id,
    gateway_link_config_status_t status,
    gateway_link_message_t *message)
{
    if (message == NULL) return false;
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_CONFIG_RESULT;
    const gateway_link_config_result_t result = {
        .request_id = request_id,
        .status = status,
    };
    return gateway_link_encode_config_result_payload(
        &result, message->payload, sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}
'''

test_stream = r'''#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_stream.h"

static size_t make_ping(uint32_t sequence, uint32_t token, uint8_t *encoded)
{
    gateway_link_frame_t frame = {
        .type = GATEWAY_LINK_MSG_PING,
        .sequence = sequence,
    };
    assert(gateway_link_encode_u32_payload(
        token, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    size_t length = 0U;
    assert(gateway_link_encode_frame(
        &frame, encoded, GATEWAY_LINK_MAX_FRAME_BYTES, &length) == GATEWAY_LINK_OK);
    return length;
}

static void test_partial_frame(void)
{
    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t length = make_ping(3U, 0x12345678UL, encoded);
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    for (size_t i = 0U; i + 1U < length; ++i) {
        assert(gateway_link_stream_feed(&decoder, encoded[i], &frame, &result) == GATEWAY_LINK_STREAM_NONE);
    }
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.type == GATEWAY_LINK_MSG_PING);
    assert(frame.sequence == 3U);
    uint32_t token = 0U;
    assert(gateway_link_decode_u32_payload(frame.payload, frame.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == 0x12345678UL);
}

static void test_corrupt_then_resync(void)
{
    uint8_t bad[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t bad_length = make_ping(1U, 1U, bad);
    bad[5] ^= 0x55U;
    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(2U, 2U, good);

    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    gateway_link_stream_event_t event = GATEWAY_LINK_STREAM_NONE;
    for (size_t i = 0U; i < bad_length; ++i) {
        event = gateway_link_stream_feed(&decoder, bad[i], &frame, &result);
    }
    assert(event == GATEWAY_LINK_STREAM_DROPPED);
    for (size_t i = 0U; i < good_length; ++i) {
        event = gateway_link_stream_feed(&decoder, good[i], &frame, &result);
    }
    assert(event == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 2U);
}

static void test_overflow_recovers_on_delimiter(void)
{
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    gateway_link_frame_t frame = {0};
    gateway_link_result_t result = GATEWAY_LINK_OK;
    for (size_t i = 0U; i < GATEWAY_LINK_MAX_FRAME_BYTES + 10U; ++i) {
        (void)gateway_link_stream_feed(&decoder, 0x7fU, &frame, &result);
    }
    assert(gateway_link_stream_feed(&decoder, 0U, &frame, &result) == GATEWAY_LINK_STREAM_DROPPED);
    assert(result == GATEWAY_LINK_BUFFER_TOO_SMALL);

    uint8_t good[GATEWAY_LINK_MAX_FRAME_BYTES];
    const size_t good_length = make_ping(9U, 10U, good);
    gateway_link_stream_event_t event = GATEWAY_LINK_STREAM_NONE;
    for (size_t i = 0U; i < good_length; ++i) {
        event = gateway_link_stream_feed(&decoder, good[i], &frame, &result);
    }
    assert(event == GATEWAY_LINK_STREAM_FRAME);
    assert(frame.sequence == 9U);
}

int main(void)
{
    test_partial_frame();
    test_corrupt_then_resync();
    test_overflow_recovers_on_delimiter();
    puts("gateway_link_stream host tests passed");
    return 0;
}
'''

test_control = r'''#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_control.h"

static gateway_link_frame_t make_hello(uint8_t type, gateway_link_role_t role, uint8_t min_v, uint8_t max_v)
{
    gateway_link_frame_t frame = {.type = type};
    const gateway_link_hello_t hello = {
        .role = role,
        .min_version = min_v,
        .max_version = max_v,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = 0U,
    };
    assert(gateway_link_encode_hello_payload(
        &hello, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    return frame;
}

static void test_hello_compatibility(void)
{
    gateway_link_frame_t frame = make_hello(
        GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST, 1U, 1U);
    gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_HELLO_ACK);
    assert(gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_S3_HOST, 2U, 2U);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));

    frame = make_hello(GATEWAY_LINK_MSG_HELLO_ACK, GATEWAY_LINK_ROLE_C6_GATEWAY, 1U, 1U);
    action = gateway_link_control_parse(&frame);
    assert(!gateway_link_control_peer_compatible(&action.peer_hello));
}

static void test_ping_and_pong(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_PING};
    assert(gateway_link_encode_u32_payload(
        0xdeadbeefUL, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_PING);
    assert(action.token == 0xdeadbeefUL);

    gateway_link_message_t pong;
    assert(gateway_link_make_pong_message(action.token, &pong));
    uint32_t token = 0U;
    assert(pong.type == GATEWAY_LINK_MSG_PONG);
    assert(gateway_link_decode_u32_payload(pong.payload, pong.payload_length, &token) == GATEWAY_LINK_OK);
    assert(token == action.token);
}

static void test_permit_join_and_result(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_PERMIT_JOIN};
    const gateway_link_permit_join_t command = {
        .request_id = 42U,
        .duration_seconds = 180U,
    };
    assert(gateway_link_encode_permit_join_payload(
        &command, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_PERMIT_JOIN);
    assert(action.request_id == 42U);
    assert(action.permit_join_seconds == 180U);

    gateway_link_message_t response;
    assert(gateway_link_make_config_result_message(
        action.request_id, GATEWAY_LINK_CONFIG_APPLIED, &response));
    gateway_link_config_result_t decoded = {0};
    assert(gateway_link_decode_config_result_payload(
        response.payload, response.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == 42U);
    assert(decoded.status == GATEWAY_LINK_CONFIG_APPLIED);
}

static void test_measurement_policy_truthfully_unsupported(void)
{
    gateway_link_frame_t frame = {.type = GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY};
    gateway_link_measurement_policy_t policy = {0};
    policy.request_id = 77U;
    policy.input.source = GATEWAY_SOURCE_LOCAL_I2C;
    strcpy(policy.input.id, "scd4x:a12bef073b43");
    policy.kind = GATEWAY_MEAS_TEMPERATURE;
    policy.min_interval_ms = 5000U;
    policy.max_interval_ms = 60000U;
    policy.reportable_change = 0.2;
    assert(gateway_link_encode_measurement_policy_payload(
        &policy, frame.payload, sizeof(frame.payload), &frame.payload_length) == GATEWAY_LINK_OK);
    const gateway_link_control_action_t action = gateway_link_control_parse(&frame);
    assert(action.kind == GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED);
    assert(action.request_id == 77U);
}

static void test_hello_ack_features(void)
{
    gateway_link_message_t message;
    assert(gateway_link_make_hello_ack_message(&message));
    gateway_link_hello_t hello = {0};
    assert(gateway_link_decode_hello_payload(
        message.payload, message.payload_length, &hello) == GATEWAY_LINK_OK);
    assert(hello.features == GATEWAY_LINK_FEATURE_PERMIT_JOIN);
}

int main(void)
{
    test_hello_compatibility();
    test_ping_and_pong();
    test_permit_join_and_result();
    test_measurement_policy_truthfully_unsupported();
    test_hello_ack_features();
    puts("gateway_link_control host tests passed");
    return 0;
}
'''

(ROOT / 'main/gateway_link_stream.h').write_text(stream_h)
(ROOT / 'main/gateway_link_stream.c').write_text(stream_c)
(ROOT / 'main/gateway_link_control.h').write_text(control_h)
(ROOT / 'main/gateway_link_control.c').write_text(control_c)
(ROOT / 'tests/host/test_gateway_link_stream.c').write_text(test_stream)
(ROOT / 'tests/host/test_gateway_link_control.c').write_text(test_control)

# Update HELLO feature declaration now that PERMIT_JOIN RX is actually implemented.
adapter_path = ROOT / 'main/gateway_link_event_adapter.c'
adapter = adapter_path.read_text()
old = '        .features = 0U,\n'
assert old in adapter
adapter = adapter.replace(old, '        .features = GATEWAY_LINK_FEATURE_PERMIT_JOIN,\n', 1)
adapter_path.write_text(adapter)

# Add stream/control sources and RX task to UART runtime.
uart_path = ROOT / 'main/gateway_uart_link.c'
uart = uart_path.read_text()
uart = uart.replace('#include "gateway_link_event_adapter.h"\n', '#include "gateway_link_event_adapter.h"\n#include "gateway_link_control.h"\n#include "gateway_link_stream.h"\n#include "zigbee_gateway.h"\n', 1)
uart = uart.replace('#define LINK_TX_TASK_PRIORITY 4U\n', '#define LINK_TX_TASK_PRIORITY 4U\n#define LINK_RX_TASK_STACK_BYTES 4096U\n#define LINK_RX_TASK_PRIORITY 4U\n#define LINK_RX_READ_BYTES 64U\n', 1)
insert_before = '''static void tx_task(void *arg)\n'''
assert insert_before in uart
rx_block = r'''static void handle_control_frame(const gateway_link_frame_t *frame)
{
    const gateway_link_control_action_t action = gateway_link_control_parse(frame);
    gateway_link_message_t response;
    switch (action.kind) {
    case GATEWAY_LINK_CONTROL_HELLO:
        if (!gateway_link_control_peer_compatible(&action.peer_hello)) {
            ESP_LOGW(TAG, "incompatible GatewayLink HELLO from peer");
            return;
        }
        ESP_LOGI(TAG, "GatewayLink S3 peer compatible (HELLO)");
        if (gateway_link_make_hello_ack_message(&response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_HELLO_ACK:
        if (gateway_link_control_peer_compatible(&action.peer_hello)) {
            ESP_LOGI(TAG, "GatewayLink S3 peer ready");
        } else {
            ESP_LOGW(TAG, "incompatible GatewayLink HELLO_ACK from peer");
        }
        break;
    case GATEWAY_LINK_CONTROL_PING:
        if (gateway_link_make_pong_message(action.token, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_PERMIT_JOIN:
        zigbee_gateway_set_permit_join(action.permit_join_seconds);
        if (gateway_link_make_config_result_message(
                action.request_id, GATEWAY_LINK_CONFIG_APPLIED, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_MEASUREMENT_POLICY_UNSUPPORTED:
        if (gateway_link_make_config_result_message(
                action.request_id, GATEWAY_LINK_CONFIG_UNSUPPORTED, &response)) {
            (void)enqueue_message(&response);
        }
        break;
    case GATEWAY_LINK_CONTROL_INVALID:
        ESP_LOGW(TAG, "invalid GatewayLink control payload type=0x%02x", frame->type);
        break;
    case GATEWAY_LINK_CONTROL_IGNORE:
    default:
        break;
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    gateway_link_stream_decoder_t decoder;
    gateway_link_stream_init(&decoder);
    uint8_t bytes[LINK_RX_READ_BYTES];
    for (;;) {
        const int count = uart_read_bytes(
            LINK_UART, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
        if (count <= 0) {
            continue;
        }
        for (int i = 0; i < count; ++i) {
            gateway_link_frame_t frame = {0};
            gateway_link_result_t decode_result = GATEWAY_LINK_OK;
            const gateway_link_stream_event_t event = gateway_link_stream_feed(
                &decoder, bytes[i], &frame, &decode_result);
            if (event == GATEWAY_LINK_STREAM_FRAME) {
                handle_control_frame(&frame);
            } else if (event == GATEWAY_LINK_STREAM_DROPPED) {
                ESP_LOGW(TAG, "dropped invalid GatewayLink RX frame error=%u", (unsigned)decode_result);
            }
        }
    }
}

'''
uart = uart.replace(insert_before, rx_block + insert_before, 1)
old_start = '''    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,\n                    LINK_TX_TASK_PRIORITY, NULL) != pdPASS) {\n        uart_driver_delete(LINK_UART);\n        s_tx_queue = NULL;\n        return ESP_ERR_NO_MEM;\n    }\n\n    gateway_link_message_t hello;\n'''
new_start = '''    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,\n                    LINK_TX_TASK_PRIORITY, NULL) != pdPASS) {\n        uart_driver_delete(LINK_UART);\n        s_tx_queue = NULL;\n        return ESP_ERR_NO_MEM;\n    }\n    if (xTaskCreate(rx_task, "gateway_uart_rx", LINK_RX_TASK_STACK_BYTES, NULL,\n                    LINK_RX_TASK_PRIORITY, NULL) != pdPASS) {\n        ESP_LOGE(TAG, "failed to create GatewayLink RX task");\n        return ESP_ERR_NO_MEM;\n    }\n\n    gateway_link_message_t hello;\n'''
assert old_start in uart
uart = uart.replace(old_start, new_start, 1)
uart_path.write_text(uart)

# Add sources to firmware CMake.
cmake_path = ROOT / 'main/CMakeLists.txt'
cmake = cmake_path.read_text()
needle = '        "gateway_link_event_adapter.c"\n'
assert needle in cmake
cmake = cmake.replace(needle, needle + '        "gateway_link_stream.c"\n        "gateway_link_control.c"\n', 1)
cmake_path.write_text(cmake)

# Add host tests.
workflow_path = ROOT / '.github/workflows/quality.yml'
workflow = workflow_path.read_text()
marker = '  host-reporting-policy:\n'
assert marker in workflow
jobs = '''  host-link-stream:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build GatewayLink stream host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -Imain \\\n            tests/host/test_gateway_link_stream.c \\\n            main/gateway_link_stream.c \\\n            main/gateway_link_protocol.c \\\n            -lm \\\n            -o /tmp/test_gateway_link_stream\n      - name: Run GatewayLink stream host tests\n        run: /tmp/test_gateway_link_stream\n\n  host-link-control:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build GatewayLink control host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -Imain \\\n            tests/host/test_gateway_link_control.c \\\n            main/gateway_link_control.c \\\n            main/gateway_link_protocol.c \\\n            -lm \\\n            -o /tmp/test_gateway_link_control\n      - name: Run GatewayLink control host tests\n        run: /tmp/test_gateway_link_control\n\n'''
workflow = workflow.replace(marker, jobs + marker, 1)
workflow_path.write_text(workflow)

# Docs: features match implementation and snapshot/policy are still future.
doc_path = ROOT / 'docs/GATEWAY_LINK_V1.md'
doc = doc_path.read_text()
old = 'The C6 application link uses UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The RX pin has an internal pull-up so an unconnected S3 does not create a floating UART input. The current firmware stage transmits HELLO, input descriptors and normalized measurements through a bounded TX queue; RX/control handling is added separately and therefore HELLO currently advertises no optional feature bits.\n'
new = 'The C6 application link uses UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The RX pin has an internal pull-up so an unconnected S3 does not create a floating UART input. TX uses a bounded queue and RX uses a delimiter-resynchronizing stream decoder. The C6 currently implements HELLO/HELLO_ACK, PING/PONG and PERMIT_JOIN control in addition to descriptor/measurement TX. Snapshot and measurement-policy application are not advertised until their state/adapter layers exist.\n'
assert old in doc
doc = doc.replace(old, new, 1)
doc_path.write_text(doc)

arch_path = ROOT / 'docs/ARCHITECTURE.md'
arch = arch_path.read_text()
old = '- `gateway_uart_link.c/.h` owns UART1 on TX GPIO18 / RX GPIO19 at 460800 8-N-1 and a bounded non-blocking TX queue. The UART task may block on hardware writes; gateway event handling never does. The current stage is transmit-only and advertises no RX/control feature bits yet.\n'
new = '- `gateway_link_stream.c/.h` is the pure incremental COBS frame stream decoder. Oversize/corrupt frames are dropped at the next delimiter so later frames resynchronize without dynamic allocation.\n- `gateway_link_control.c/.h` parses source-neutral link control and builds replies. It owns protocol semantics, not UART I/O or Zigbee implementation.\n- `gateway_uart_link.c/.h` owns UART1 on TX GPIO18 / RX GPIO19 at 460800 8-N-1, bounded TX queuing and the RX task. The UART tasks may block on driver I/O; gateway event handling never does. RX currently implements peer handshake, ping/pong and permit-join dispatch; unsupported measurement-policy requests receive an explicit UNSUPPORTED result.\n'
assert old in arch
arch = arch.replace(old, new, 1)
arch_path.write_text(arch)

readme_path = ROOT / 'README.md'
readme = readme_path.read_text()
old = 'The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements, snapshots and source-neutral measurement policy requests. The C6 now has the transmit path on UART1 at 460800 baud using TX GPIO18 / RX GPIO19; a bounded queue forwards only normalized input descriptors and measurements, so UART backpressure cannot block Zigbee callbacks or local sensor tasks. Bidirectional RX/control handling is a separate stage.\n'
new = 'The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements and versioned controls. UART1 runs at 460800 baud on TX GPIO18 / RX GPIO19. TX is bounded/non-blocking to event handling; RX resynchronizes at COBS delimiters and currently handles HELLO/ACK, PING/PONG and `PERMIT_JOIN`. Snapshot and source-neutral measurement-policy application remain disabled until their real backing state/policy layers are implemented.\n'
assert old in readme
readme = readme.replace(old, new, 1)
readme_path.write_text(readme)

for path in [
    ROOT / 'main/gateway_link_stream.h', ROOT / 'main/gateway_link_stream.c',
    ROOT / 'main/gateway_link_control.h', ROOT / 'main/gateway_link_control.c',
    ROOT / 'tests/host/test_gateway_link_stream.c', ROOT / 'tests/host/test_gateway_link_control.c'
]:
    assert b'\x00' not in path.read_bytes()
