from pathlib import Path

ROOT = Path('.')

adapter_h = r'''#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_events.h"
#include "gateway_link_protocol.h"

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t payload_length;
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
} gateway_link_message_t;

bool gateway_link_message_from_event(
    const gateway_event_t *event,
    gateway_link_message_t *message);

bool gateway_link_make_hello_message(gateway_link_message_t *message);
'''

adapter_c = r'''#include "gateway_link_event_adapter.h"

#include <string.h>

bool gateway_link_make_hello_message(gateway_link_message_t *message)
{
    if (message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));
    message->type = GATEWAY_LINK_MSG_HELLO;
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_version = GATEWAY_LINK_PROTOCOL_VERSION,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = 0U,
    };
    return gateway_link_encode_hello_payload(
        &hello,
        message->payload,
        sizeof(message->payload),
        &message->payload_length) == GATEWAY_LINK_OK;
}

bool gateway_link_message_from_event(
    const gateway_event_t *event,
    gateway_link_message_t *message)
{
    if (event == NULL || message == NULL) {
        return false;
    }
    memset(message, 0, sizeof(*message));

    if (event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ||
        event->kind == GATEWAY_EVENT_INPUT_UNAVAILABLE) {
        gateway_link_input_descriptor_t descriptor = {
            .input = event->input,
            .available = event->kind == GATEWAY_EVENT_INPUT_AVAILABLE,
            .capabilities = event->data.input_desc.capabilities,
        };
        strncpy(descriptor.model, event->data.input_desc.model, sizeof(descriptor.model) - 1U);
        message->type = GATEWAY_LINK_MSG_INPUT_DESCRIPTOR;
        return gateway_link_encode_input_descriptor_payload(
            &descriptor,
            message->payload,
            sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    if (event->kind == GATEWAY_EVENT_MEASUREMENT) {
        const gateway_link_measurement_t measurement = {
            .input = event->input,
            .uptime_ms = event->uptime_ms,
            .measurement = event->data.measurement,
            .quality = GATEWAY_LINK_QUALITY_VALID,
        };
        message->type = GATEWAY_LINK_MSG_MEASUREMENT;
        return gateway_link_encode_measurement_payload(
            &measurement,
            message->payload,
            sizeof(message->payload),
            &message->payload_length) == GATEWAY_LINK_OK;
    }

    return false;
}
'''

uart_h = r'''#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gateway_events.h"

#define GATEWAY_UART_LINK_TX_GPIO 18
#define GATEWAY_UART_LINK_RX_GPIO 19
#define GATEWAY_UART_LINK_BAUD_RATE 460800

esp_err_t gateway_uart_link_start(void);
void gateway_uart_link_publish_event(const gateway_event_t *event);
uint32_t gateway_uart_link_take_dropped(void);
'''

uart_c = r'''#include "gateway_uart_link.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_link_event_adapter.h"
#include "gateway_link_protocol.h"

#define LINK_UART UART_NUM_1
#define LINK_TX_QUEUE_DEPTH 16U
#define LINK_UART_RX_BUFFER_BYTES 1024
#define LINK_UART_TX_BUFFER_BYTES 1024
#define LINK_TX_TASK_STACK_BYTES 4096U
#define LINK_TX_TASK_PRIORITY 4U

static const char *TAG = "gateway_uart_link";

typedef struct {
    uint32_t sequence;
    gateway_link_message_t message;
} tx_item_t;

static StaticQueue_t s_tx_queue_buffer;
static uint8_t s_tx_queue_storage[LINK_TX_QUEUE_DEPTH * sizeof(tx_item_t)];
static QueueHandle_t s_tx_queue;
static uint32_t s_next_sequence = 1U;
static uint32_t s_dropped;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t allocate_sequence(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const uint32_t value = s_next_sequence++;
    portEXIT_CRITICAL(&s_state_lock);
    return value;
}

static void note_drop(void)
{
    portENTER_CRITICAL(&s_state_lock);
    ++s_dropped;
    portEXIT_CRITICAL(&s_state_lock);
}

uint32_t gateway_uart_link_take_dropped(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const uint32_t value = s_dropped;
    s_dropped = 0U;
    portEXIT_CRITICAL(&s_state_lock);
    return value;
}

static bool enqueue_message(const gateway_link_message_t *message)
{
    if (message == NULL || s_tx_queue == NULL) {
        return false;
    }
    const tx_item_t item = {
        .sequence = allocate_sequence(),
        .message = *message,
    };
    if (xQueueSend(s_tx_queue, &item, 0U) != pdPASS) {
        note_drop();
        return false;
    }
    return true;
}

void gateway_uart_link_publish_event(const gateway_event_t *event)
{
    gateway_link_message_t message;
    if (gateway_link_message_from_event(event, &message)) {
        (void)enqueue_message(&message);
    }
}

static void tx_task(void *arg)
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

esp_err_t gateway_uart_link_start(void)
{
    if (s_tx_queue != NULL) {
        return ESP_OK;
    }

    s_tx_queue = xQueueCreateStatic(
        LINK_TX_QUEUE_DEPTH,
        sizeof(tx_item_t),
        s_tx_queue_storage,
        &s_tx_queue_buffer);
    if (s_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t config = {
        .baud_rate = GATEWAY_UART_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t result = uart_driver_install(
        LINK_UART,
        LINK_UART_RX_BUFFER_BYTES,
        LINK_UART_TX_BUFFER_BYTES,
        0,
        NULL,
        0);
    if (result != ESP_OK) {
        s_tx_queue = NULL;
        return result;
    }
    result = uart_param_config(LINK_UART, &config);
    if (result != ESP_OK) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return result;
    }
    result = uart_set_pin(
        LINK_UART,
        GATEWAY_UART_LINK_TX_GPIO,
        GATEWAY_UART_LINK_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return result;
    }
    (void)gpio_set_pull_mode((gpio_num_t)GATEWAY_UART_LINK_RX_GPIO, GPIO_PULLUP_ONLY);

    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,
                    LINK_TX_TASK_PRIORITY, NULL) != pdPASS) {
        uart_driver_delete(LINK_UART);
        s_tx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    gateway_link_message_t hello;
    if (!gateway_link_make_hello_message(&hello) || !enqueue_message(&hello)) {
        ESP_LOGW(TAG, "failed to queue initial GatewayLink HELLO");
    }
    ESP_LOGI(TAG, "GatewayLink UART1 TX=%d RX=%d baud=%d",
             GATEWAY_UART_LINK_TX_GPIO, GATEWAY_UART_LINK_RX_GPIO,
             GATEWAY_UART_LINK_BAUD_RATE);
    return ESP_OK;
}
'''

test = r'''#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_event_adapter.h"

static gateway_input_id_t scd41_input(void)
{
    gateway_input_id_t input = {0};
    input.source = GATEWAY_SOURCE_LOCAL_I2C;
    input.channel = 0U;
    strcpy(input.id, "scd4x:a12bef073b43");
    return input;
}

static void test_hello_truthfully_advertises_tx_only_stage(void)
{
    gateway_link_message_t message;
    assert(gateway_link_make_hello_message(&message));
    assert(message.type == GATEWAY_LINK_MSG_HELLO);
    gateway_link_hello_t hello = {0};
    assert(gateway_link_decode_hello_payload(
        message.payload, message.payload_length, &hello) == GATEWAY_LINK_OK);
    assert(hello.role == GATEWAY_LINK_ROLE_C6_GATEWAY);
    assert(hello.min_version == 1U && hello.max_version == 1U);
    assert(hello.max_frame_bytes == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(hello.features == 0U);
}

static void test_input_descriptor_event(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_INPUT_AVAILABLE;
    event.input = scd41_input();
    event.data.input_desc.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY | GATEWAY_INPUT_CAP_CO2;
    strcpy(event.data.input_desc.model, "SCD41");

    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_INPUT_DESCRIPTOR);
    gateway_link_input_descriptor_t decoded = {0};
    assert(gateway_link_decode_input_descriptor_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.available);
    assert(strcmp(decoded.input.id, "scd4x:a12bef073b43") == 0);
    assert(decoded.capabilities == 0x13U);
    assert(strcmp(decoded.model, "SCD41") == 0);

    event.kind = GATEWAY_EVENT_INPUT_UNAVAILABLE;
    assert(gateway_link_message_from_event(&event, &message));
    assert(gateway_link_decode_input_descriptor_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(!decoded.available);
}

static void test_measurement_event(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_MEASUREMENT;
    event.input = scd41_input();
    event.uptime_ms = 5887U;
    event.data.measurement.kind = GATEWAY_MEAS_CO2;
    event.data.measurement.unit = GATEWAY_UNIT_PPM;
    event.data.measurement.value = 1123.0;

    gateway_link_message_t message;
    assert(gateway_link_message_from_event(&event, &message));
    assert(message.type == GATEWAY_LINK_MSG_MEASUREMENT);
    gateway_link_measurement_t decoded = {0};
    assert(gateway_link_decode_measurement_payload(
        message.payload, message.payload_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.uptime_ms == 5887U);
    assert(decoded.measurement.kind == GATEWAY_MEAS_CO2);
    assert(decoded.measurement.unit == GATEWAY_UNIT_PPM);
    assert(fabs(decoded.measurement.value - 1123.0) < 0.000001);
    assert(decoded.quality == GATEWAY_LINK_QUALITY_VALID);
}

static void test_protocol_specific_event_is_not_forwarded(void)
{
    gateway_event_t event = {0};
    event.kind = GATEWAY_EVENT_RAW_ATTRIBUTE;
    gateway_link_message_t message;
    assert(!gateway_link_message_from_event(&event, &message));

    event.kind = GATEWAY_EVENT_DEVICE_ANNOUNCE;
    assert(!gateway_link_message_from_event(&event, &message));
}

int main(void)
{
    test_hello_truthfully_advertises_tx_only_stage();
    test_input_descriptor_event();
    test_measurement_event();
    test_protocol_specific_event_is_not_forwarded();
    puts("gateway_link_event_adapter host tests passed");
    return 0;
}
'''

(ROOT / 'main/gateway_link_event_adapter.h').write_text(adapter_h)
(ROOT / 'main/gateway_link_event_adapter.c').write_text(adapter_c)
(ROOT / 'main/gateway_uart_link.h').write_text(uart_h)
(ROOT / 'main/gateway_uart_link.c').write_text(uart_c)
(ROOT / 'tests/host/test_gateway_link_event_adapter.c').write_text(test)

cmake_path = ROOT / 'main/CMakeLists.txt'
cmake = cmake_path.read_text()
needle = '        "gateway_link_protocol.c"\n'
assert needle in cmake
cmake = cmake.replace(needle, '        "gateway_link_protocol.c"\n        "gateway_link_event_adapter.c"\n        "gateway_uart_link.c"\n', 1)
cmake = cmake.replace('REQUIRES nvs_flash esp_timer esp_driver_i2c', 'REQUIRES nvs_flash esp_timer esp_driver_i2c esp_driver_uart')
cmake_path.write_text(cmake)

app_path = ROOT / 'main/app_main.c'
app = app_path.read_text()
assert '#include "gateway_transport.h"\n' in app
app = app.replace('#include "gateway_transport.h"\n', '#include "gateway_transport.h"\n#include "gateway_uart_link.h"\n', 1)
assert '    ESP_ERROR_CHECK(gateway_transport_start());\n' in app
app = app.replace('    ESP_ERROR_CHECK(gateway_transport_start());\n', '    ESP_ERROR_CHECK(gateway_uart_link_start());\n    ESP_ERROR_CHECK(gateway_transport_start());\n', 1)
app_path.write_text(app)

transport_path = ROOT / 'main/gateway_transport.c'
transport = transport_path.read_text()
assert '#include "gateway_events.h"\n' in transport
transport = transport.replace('#include "gateway_events.h"\n', '#include "gateway_events.h"\n#include "gateway_uart_link.h"\n', 1)
old = '''        if (gateway_event_receive(&event, pdMS_TO_TICKS(1000))) {\n            log_event(&event);\n        }\n        uint32_t dropped = gateway_event_take_dropped();\n'''
new = '''        if (gateway_event_receive(&event, pdMS_TO_TICKS(1000))) {\n            log_event(&event);\n            gateway_uart_link_publish_event(&event);\n        }\n        const uint32_t link_dropped = gateway_uart_link_take_dropped();\n        if (link_dropped != 0U) {\n            ESP_LOGW(TAG, "dropped %" PRIu32 " GatewayLink messages because the UART TX queue was full", link_dropped);\n        }\n        uint32_t dropped = gateway_event_take_dropped();\n'''
assert old in transport
transport = transport.replace(old, new, 1)
transport_path.write_text(transport)

workflow_path = ROOT / '.github/workflows/quality.yml'
workflow = workflow_path.read_text()
marker = '  host-reporting-policy:\n'
assert marker in workflow
job = '''  host-link-event-adapter:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build GatewayLink event adapter host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -Imain \\\n            tests/host/test_gateway_link_event_adapter.c \\\n            main/gateway_link_event_adapter.c \\\n            main/gateway_link_protocol.c \\\n            -lm \\\n            -o /tmp/test_gateway_link_event_adapter\n      - name: Run GatewayLink event adapter host tests\n        run: /tmp/test_gateway_link_event_adapter\n\n'''
workflow = workflow.replace(marker, job + marker, 1)
workflow_path.write_text(workflow)

arch_path = ROOT / 'docs/ARCHITECTURE.md'
arch = arch_path.read_text()
needle = '- `gateway_link_protocol.c/.h` defines the hardware-independent GatewayLink v1 framing and payload codec for the future C6-to-S3 link. It uses COBS framing, CRC32, explicit little-endian wire values and fixed-size buffers; it never serializes raw C structs.\n'
assert needle in arch
addition = '- `gateway_link_event_adapter.c/.h` is the pure mapping from normalized gateway input events to GatewayLink messages. Protocol-specific Zigbee lifecycle/raw events are intentionally not forwarded.\n- `gateway_uart_link.c/.h` owns UART1 on TX GPIO18 / RX GPIO19 at 460800 8-N-1 and a bounded non-blocking TX queue. The UART task may block on hardware writes; gateway event handling never does. The current stage is transmit-only and advertises no RX/control feature bits yet.\n'
arch = arch.replace(needle, needle + addition, 1)
arch_path.write_text(arch)

link_doc_path = ROOT / 'docs/GATEWAY_LINK_V1.md'
link_doc = link_doc_path.read_text()
old = 'The intended C6 application UART is UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The v1 codec is hardware-independent; UART initialization is deliberately not part of this stage.\n'
new = 'The C6 application link uses UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The RX pin has an internal pull-up so an unconnected S3 does not create a floating UART input. The current firmware stage transmits HELLO, input descriptors and normalized measurements through a bounded TX queue; RX/control handling is added separately and therefore HELLO currently advertises no optional feature bits.\n'
assert old in link_doc
link_doc = link_doc.replace(old, new, 1)
link_doc_path.write_text(link_doc)

readme_path = ROOT / 'README.md'
readme = readme_path.read_text()
old = 'The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements, snapshots and source-neutral measurement policy requests. The codec is host-tested and hardware-independent; the UART1 GPIO18/GPIO19 driver is a separate integration stage.\n'
new = 'The protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements, snapshots and source-neutral measurement policy requests. The C6 now has the transmit path on UART1 at 460800 baud using TX GPIO18 / RX GPIO19; a bounded queue forwards only normalized input descriptors and measurements, so UART backpressure cannot block Zigbee callbacks or local sensor tasks. Bidirectional RX/control handling is a separate stage.\n'
assert old in readme
readme = readme.replace(old, new, 1)
readme_path.write_text(readme)

for path in [
    ROOT / 'main/gateway_link_event_adapter.h', ROOT / 'main/gateway_link_event_adapter.c',
    ROOT / 'main/gateway_uart_link.h', ROOT / 'main/gateway_uart_link.c',
    ROOT / 'tests/host/test_gateway_link_event_adapter.c'
]:
    assert b'\x00' not in path.read_bytes()
