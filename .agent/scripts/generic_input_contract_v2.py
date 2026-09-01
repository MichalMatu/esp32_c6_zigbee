from pathlib import Path

Path('main/gateway_inputs.h').write_text(r'''#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GATEWAY_INPUT_ID_MAX_BYTES 40U

typedef enum {
    GATEWAY_SOURCE_ZIGBEE,
    GATEWAY_SOURCE_LOCAL_I2C,
} gateway_source_t;

typedef struct {
    gateway_source_t source;
    char id[GATEWAY_INPUT_ID_MAX_BYTES];
    uint8_t channel;
} gateway_input_id_t;

typedef enum {
    GATEWAY_MEAS_TEMPERATURE,
    GATEWAY_MEAS_HUMIDITY,
    GATEWAY_MEAS_ILLUMINANCE,
    GATEWAY_MEAS_OCCUPANCY,
    GATEWAY_MEAS_CO2,
    GATEWAY_MEAS_BATTERY_VOLTAGE,
    GATEWAY_MEAS_BATTERY_PERCENT,
    GATEWAY_MEAS_MAINS_VOLTAGE,
    GATEWAY_MEAS_VOLTAGE,
    GATEWAY_MEAS_CURRENT,
    GATEWAY_MEAS_POWER,
    GATEWAY_MEAS_ENERGY,
    GATEWAY_MEAS_ON_OFF,
} gateway_measurement_kind_t;

typedef enum {
    GATEWAY_UNIT_NONE,
    GATEWAY_UNIT_CELSIUS,
    GATEWAY_UNIT_PERCENT,
    GATEWAY_UNIT_LUX_LOG,
    GATEWAY_UNIT_PPM,
    GATEWAY_UNIT_VOLTS,
    GATEWAY_UNIT_AMPS,
    GATEWAY_UNIT_WATTS,
    GATEWAY_UNIT_KILOWATT_HOURS,
    GATEWAY_UNIT_BOOLEAN,
} gateway_unit_t;

typedef uint32_t gateway_input_capabilities_t;

#define GATEWAY_INPUT_CAP_TEMPERATURE     (1UL << 0)
#define GATEWAY_INPUT_CAP_HUMIDITY        (1UL << 1)
#define GATEWAY_INPUT_CAP_ILLUMINANCE     (1UL << 2)
#define GATEWAY_INPUT_CAP_OCCUPANCY       (1UL << 3)
#define GATEWAY_INPUT_CAP_CO2             (1UL << 4)
#define GATEWAY_INPUT_CAP_BATTERY_VOLTAGE (1UL << 5)
#define GATEWAY_INPUT_CAP_BATTERY_PERCENT (1UL << 6)
#define GATEWAY_INPUT_CAP_MAINS_VOLTAGE   (1UL << 7)
#define GATEWAY_INPUT_CAP_VOLTAGE         (1UL << 8)
#define GATEWAY_INPUT_CAP_CURRENT         (1UL << 9)
#define GATEWAY_INPUT_CAP_POWER           (1UL << 10)
#define GATEWAY_INPUT_CAP_ENERGY          (1UL << 11)
#define GATEWAY_INPUT_CAP_ON_OFF          (1UL << 12)

typedef struct {
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;
} gateway_measurement_t;

gateway_input_id_t gateway_input_make(
    gateway_source_t source, const char *id, uint8_t channel);

gateway_input_id_t gateway_input_make_zigbee(
    const uint8_t ieee[8], bool ieee_valid, uint16_t short_addr, uint8_t endpoint);

gateway_input_capabilities_t gateway_input_capability_for_measurement(
    gateway_measurement_kind_t kind);

const char *gateway_input_source_name(gateway_source_t source);
''')

Path('main/gateway_inputs.c').write_text(r'''#include "gateway_inputs.h"

#include <stdio.h>
#include <string.h>

gateway_input_id_t gateway_input_make(
    gateway_source_t source, const char *id, uint8_t channel)
{
    gateway_input_id_t input = {
        .source = source,
        .channel = channel,
    };
    if (id != NULL) {
        strncpy(input.id, id, sizeof(input.id) - 1U);
    }
    return input;
}

gateway_input_id_t gateway_input_make_zigbee(
    const uint8_t ieee[8], bool ieee_valid, uint16_t short_addr, uint8_t endpoint)
{
    gateway_input_id_t input = {
        .source = GATEWAY_SOURCE_ZIGBEE,
        .channel = endpoint,
    };
    if (ieee_valid && ieee != NULL) {
        snprintf(
            input.id, sizeof(input.id),
            "zigbee:%02x%02x%02x%02x%02x%02x%02x%02x",
            ieee[7], ieee[6], ieee[5], ieee[4],
            ieee[3], ieee[2], ieee[1], ieee[0]);
    } else {
        snprintf(input.id, sizeof(input.id), "zigbee-short:%04x", short_addr);
    }
    return input;
}

gateway_input_capabilities_t gateway_input_capability_for_measurement(
    gateway_measurement_kind_t kind)
{
    static const gateway_input_capabilities_t capabilities[] = {
        GATEWAY_INPUT_CAP_TEMPERATURE,
        GATEWAY_INPUT_CAP_HUMIDITY,
        GATEWAY_INPUT_CAP_ILLUMINANCE,
        GATEWAY_INPUT_CAP_OCCUPANCY,
        GATEWAY_INPUT_CAP_CO2,
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE,
        GATEWAY_INPUT_CAP_BATTERY_PERCENT,
        GATEWAY_INPUT_CAP_MAINS_VOLTAGE,
        GATEWAY_INPUT_CAP_VOLTAGE,
        GATEWAY_INPUT_CAP_CURRENT,
        GATEWAY_INPUT_CAP_POWER,
        GATEWAY_INPUT_CAP_ENERGY,
        GATEWAY_INPUT_CAP_ON_OFF,
    };
    return kind < (sizeof(capabilities) / sizeof(capabilities[0])) ?
        capabilities[kind] : 0U;
}

const char *gateway_input_source_name(gateway_source_t source)
{
    switch (source) {
    case GATEWAY_SOURCE_ZIGBEE: return "zigbee";
    case GATEWAY_SOURCE_LOCAL_I2C: return "local_i2c";
    default: return "unknown";
    }
}
''')

Path('tests/host/test_gateway_inputs.c').write_text(r'''#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_inputs.h"

static void test_zigbee_ieee_identity(void)
{
    const uint8_t ieee[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    const gateway_input_id_t input = gateway_input_make_zigbee(
        ieee, true, 0x1234U, 7U);
    assert(input.source == GATEWAY_SOURCE_ZIGBEE);
    assert(input.channel == 7U);
    assert(strcmp(input.id, "zigbee:0807060504030201") == 0);
}

static void test_zigbee_short_fallback(void)
{
    const gateway_input_id_t input = gateway_input_make_zigbee(
        NULL, false, 0x42abU, 1U);
    assert(strcmp(input.id, "zigbee-short:42ab") == 0);
}

static void test_local_identity_is_bounded(void)
{
    const gateway_input_id_t input = gateway_input_make(
        GATEWAY_SOURCE_LOCAL_I2C,
        "scd41:001122334455:abcdefghijklmnopqrstuvwxyz", 0U);
    assert(input.source == GATEWAY_SOURCE_LOCAL_I2C);
    assert(input.channel == 0U);
    assert(input.id[GATEWAY_INPUT_ID_MAX_BYTES - 1U] == '\0');
    assert(strlen(input.id) == GATEWAY_INPUT_ID_MAX_BYTES - 1U);
}

static void test_capability_mapping(void)
{
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_TEMPERATURE) ==
           GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_HUMIDITY) ==
           GATEWAY_INPUT_CAP_HUMIDITY);
    assert(gateway_input_capability_for_measurement(GATEWAY_MEAS_CO2) ==
           GATEWAY_INPUT_CAP_CO2);
    assert(gateway_input_capability_for_measurement((gateway_measurement_kind_t)255) == 0U);
}

int main(void)
{
    test_zigbee_ieee_identity();
    test_zigbee_short_fallback();
    test_local_identity_is_bounded();
    test_capability_mapping();
    assert(strcmp(gateway_input_source_name(GATEWAY_SOURCE_ZIGBEE), "zigbee") == 0);
    assert(strcmp(gateway_input_source_name(GATEWAY_SOURCE_LOCAL_I2C), "local_i2c") == 0);
    puts("gateway_inputs host tests passed");
    return 0;
}
''')

p = Path('main/gateway_events.h')
s = p.read_text()
s = s.replace('#include <stdint.h>\n', '#include <stdint.h>\n\n#include "gateway_inputs.h"\n', 1)
start = s.index('typedef enum {\n    GATEWAY_SOURCE_ZIGBEE,')
end = s.index('typedef struct {\n    uint16_t cluster_id;', start)
s = s[:start] + s[end:]
s = s.replace(
    '    GATEWAY_EVENT_REPORTING_CONFIG,\n    GATEWAY_EVENT_MEASUREMENT,\n',
    '    GATEWAY_EVENT_REPORTING_CONFIG,\n    GATEWAY_EVENT_INPUT_AVAILABLE,\n    GATEWAY_EVENT_INPUT_UNAVAILABLE,\n    GATEWAY_EVENT_MEASUREMENT,\n', 1)
s = s.replace(
    '    gateway_device_id_t device;\n    uint8_t endpoint;\n',
    '    gateway_device_id_t device;\n    gateway_input_id_t input;\n    uint8_t endpoint;\n', 1)
old = '''        struct {
            gateway_measurement_kind_t kind;
            gateway_unit_t unit;
            double value;
            uint16_t cluster_id;
            uint16_t attribute_id;
            uint8_t zcl_type;
        } measurement;
'''
new = '''        struct {
            gateway_input_capabilities_t capabilities;
            char model[24];
        } input_desc;
        gateway_measurement_t measurement;
'''
assert old in s
s = s.replace(old, new, 1)
s = s.replace(
    'gateway_event_t gateway_event_make(\n    gateway_event_kind_t kind, const gateway_device_id_t *device);\n',
    'gateway_event_t gateway_event_make(\n    gateway_event_kind_t kind, const gateway_device_id_t *device);\ngateway_event_t gateway_event_make_input(\n    gateway_event_kind_t kind, const gateway_input_id_t *input);\n', 1)
s = s.replace(
    'bool gateway_event_warning(\n    const gateway_device_id_t *device, const char *text);\n',
    'bool gateway_event_warning(\n    const gateway_device_id_t *device, const char *text);\nbool gateway_event_warning_input(\n    const gateway_input_id_t *input, const char *text);\n', 1)
p.write_text(s)

p = Path('main/gateway_events.c')
s = p.read_text()
needle = '''    if (device != NULL) {
        event.device = *device;
    }
    return event;
}

bool gateway_event_warning(
'''
repl = '''    if (device != NULL) {
        event.device = *device;
        event.input = gateway_input_make_zigbee(
            device->ieee, device->ieee_valid, device->short_addr, 0U);
    }
    return event;
}

gateway_event_t gateway_event_make_input(
    gateway_event_kind_t kind, const gateway_input_id_t *input)
{
    gateway_event_t event = {
        .source = input == NULL ? GATEWAY_SOURCE_ZIGBEE : input->source,
        .kind = kind,
        .uptime_ms = gateway_uptime_ms(),
    };
    if (input != NULL) {
        event.input = *input;
    }
    return event;
}

bool gateway_event_warning(
'''
assert needle in s
s = s.replace(needle, repl, 1)
needle = '''    return gateway_event_publish(&event);
}

bool gateway_event_publish(const gateway_event_t *event)
'''
repl = '''    return gateway_event_publish(&event);
}

bool gateway_event_warning_input(
    const gateway_input_id_t *input, const char *text)
{
    gateway_event_t event = gateway_event_make_input(GATEWAY_EVENT_WARNING, input);
    if (text != NULL) {
        strncpy(event.data.text.value, text, sizeof(event.data.text.value) - 1U);
    }
    return gateway_event_publish(&event);
}

bool gateway_event_publish(const gateway_event_t *event)
'''
assert needle in s
s = s.replace(needle, repl, 1)
p.write_text(s)

p = Path('main/gateway_zcl_value.h')
p.write_text(p.read_text().replace('#include "gateway_events.h"', '#include "gateway_inputs.h"'))

p = Path('main/zigbee_gateway.c')
s = p.read_text()
idx = s.index('static void publish_report(')
pre, rest = s[:idx], s[idx:]
old = '''    for (ezb_zcl_report_attr_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
'''
new = '''    const gateway_input_id_t input = gateway_input_make_zigbee(
        device.ieee, device.ieee_valid, device.short_addr, header->src_ep);

    for (ezb_zcl_report_attr_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
'''
assert old in rest
rest = rest.replace(old, new, 1)
old = '''            gateway_event_t event = gateway_event_make(
                GATEWAY_EVENT_MEASUREMENT, &device
            );
            event.endpoint = header->src_ep;
            event.data.measurement = (typeof(event.data.measurement)){
                .kind = kind,
                .unit = unit,
                .value = value,
                .cluster_id = header->cluster_id,
                .attribute_id = item->attr_id,
                .zcl_type = item->attr_type,
            };
'''
new = '''            gateway_event_t event = gateway_event_make_input(
                GATEWAY_EVENT_MEASUREMENT, &input);
            event.endpoint = header->src_ep;
            event.data.measurement = (gateway_measurement_t){
                .kind = kind,
                .unit = unit,
                .value = value,
            };
'''
assert old in rest
rest = rest.replace(old, new, 1)
p.write_text(pre + rest)

p = Path('main/gateway_transport.c')
s = p.read_text()
marker = 'static void format_short_addr(uint16_t short_addr, char *out, size_t out_size)\n'
helper = '''static void format_input(const gateway_input_id_t *input, char *out, size_t out_size)
{
    const char *id = input->id[0] == '\0' ? "unknown" : input->id;
    snprintf(out, out_size, "%s/%s/ch%u",
             gateway_input_source_name(input->source), id, input->channel);
}

'''
assert marker in s
s = s.replace(marker, helper + marker, 1)
old = '''    case GATEWAY_EVENT_REPORTING_CONFIG:
        ESP_LOGI(TAG, "reporting %s ep=%u cluster=0x%04x attr=0x%04x status=0x%02x",
                 device, event->endpoint, event->data.reporting.cluster_id,
                 event->data.reporting.attribute_id, event->data.reporting.status);
        break;
    case GATEWAY_EVENT_MEASUREMENT:
        ESP_LOGI(TAG, "measurement %s ep=%u %s=%.3f %s cluster=0x%04x attr=0x%04x type=0x%02x",
                 device, event->endpoint, measurement_name(event->data.measurement.kind), event->data.measurement.value,
                 unit_name(event->data.measurement.unit), event->data.measurement.cluster_id,
                 event->data.measurement.attribute_id, event->data.measurement.zcl_type);
        break;
'''
new = '''    case GATEWAY_EVENT_REPORTING_CONFIG:
        ESP_LOGI(TAG, "reporting %s ep=%u cluster=0x%04x attr=0x%04x status=0x%02x",
                 device, event->endpoint, event->data.reporting.cluster_id,
                 event->data.reporting.attribute_id, event->data.reporting.status);
        break;
    case GATEWAY_EVENT_INPUT_AVAILABLE:
    case GATEWAY_EVENT_INPUT_UNAVAILABLE:
    {
        char input[80];
        format_input(&event->input, input, sizeof(input));
        ESP_LOGI(TAG, "input %s %s model=%s capabilities=0x%08" PRIx32,
                 event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ? "available" : "unavailable",
                 input, event->data.input_desc.model,
                 (uint32_t)event->data.input_desc.capabilities);
        break;
    }
    case GATEWAY_EVENT_MEASUREMENT:
    {
        char input[80];
        format_input(&event->input, input, sizeof(input));
        ESP_LOGI(TAG, "measurement %s %s=%.3f %s",
                 input, measurement_name(event->data.measurement.kind),
                 event->data.measurement.value, unit_name(event->data.measurement.unit));
        break;
    }
'''
assert old in s
p.write_text(s.replace(old, new, 1))

p = Path('main/CMakeLists.txt')
s = p.read_text()
needle = '        "gateway_events.c"\n        "gateway_reporting_policy.c"\n'
repl = '        "gateway_events.c"\n        "gateway_inputs.c"\n        "gateway_reporting_policy.c"\n'
assert needle in s
p.write_text(s.replace(needle, repl, 1))

p = Path('.github/workflows/quality.yml')
s = p.read_text()
marker = '  host-reporting-policy:\n'
job = '''  host-inputs:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build generic input host tests
        run: |
          cc -std=c11 -Wall -Wextra -Werror -pedantic \
            -Imain \
            tests/host/test_gateway_inputs.c \
            main/gateway_inputs.c \
            -o /tmp/test_gateway_inputs
      - name: Run generic input host tests
        run: /tmp/test_gateway_inputs

'''
assert marker in s
p.write_text(s.replace(marker, job + marker, 1))

p = Path('docs/ARCHITECTURE.md')
s = p.read_text()
s = s.replace(
    '- `gateway_events.c/.h` defines the normalized internal event contract, static event queue, drop accounting, timestamps, and common warning/event construction helpers.\n',
    '- `gateway_inputs.c/.h` defines the protocol-neutral input identity, measurement kinds/units, and capability bits. Zigbee endpoints and local buses must normalize into this contract before transport.\n- `gateway_events.c/.h` defines the normalized internal event envelope, static event queue, drop accounting, timestamps, and common warning/event construction helpers. Zigbee lifecycle metadata may remain Zigbee-specific, while measurement events carry a protocol-neutral `gateway_input_id_t`.\n', 1)
s = s.replace(
    'The event bus is the transport boundary. Zigbee code publishes normalized events; the current logger consumes them. A later UART/SPI link to another MCU should replace or extend the transport without moving Zigbee interpretation into the transport layer.\n',
    'The event bus is the transport boundary. Input adapters normalize measurements before publishing them. `gateway_transport` must consume `gateway_input_id_t` plus normalized measurements without branching on Zigbee cluster IDs or local sensor register formats. A later UART/SPI link to another MCU should serialize this normalized input contract; the ESP32-S3 can then own the current input list/state used by LiteGraph.\n\nStable input identity belongs to the adapter boundary. Zigbee uses IEEE identity plus endpoint as the logical channel; short addresses are only a provisional fallback when IEEE recovery has not completed. Local sensors use a stable hardware identity such as the SCD4x serial number, with a board-local fallback only when the device cannot expose one.\n', 1)
s = s.replace(
    '`gateway_zcl_value`, `gateway_reporting_policy`, and `gateway_device_state` have strict C11 host tests',
    '`gateway_inputs`, `gateway_zcl_value`, `gateway_reporting_policy`, and `gateway_device_state` have strict C11 host tests', 1)
p.write_text(s)

p = Path('README.md')
s = p.read_text()
s = s.replace(
    'The firmware keeps ESP Zigbee SDK integration separate from normalized events, transport, value decoding, reporting policy, device state, and console handling. The pure value/policy/state modules have strict host tests in addition to the full ESP-IDF firmware build. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module responsibilities and invariants.\n',
    'The firmware keeps ESP Zigbee SDK integration separate from a protocol-neutral input contract, normalized events, transport, value decoding, reporting policy, device state, and console handling. Zigbee is one input adapter; local I2C sensors can use the same `gateway_input_id_t` + normalized measurement boundary. The pure input/value/policy/state modules have strict host tests in addition to the full ESP-IDF firmware build. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for module responsibilities and invariants.\n', 1)
p.write_text(s)
