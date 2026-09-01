from pathlib import Path

ROOT = Path('.')

header = r'''#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gateway_inputs.h"

#define GATEWAY_LINK_PROTOCOL_VERSION 1U
#define GATEWAY_LINK_MAX_PAYLOAD 220U
#define GATEWAY_LINK_MAX_FRAME_BYTES 256U
#define GATEWAY_LINK_MODEL_MAX_BYTES 24U

#define GATEWAY_LINK_FEATURE_SNAPSHOT           (1UL << 0)
#define GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY (1UL << 1)
#define GATEWAY_LINK_FEATURE_PERMIT_JOIN        (1UL << 2)

typedef enum {
    GATEWAY_LINK_OK = 0,
    GATEWAY_LINK_INVALID_ARG,
    GATEWAY_LINK_BUFFER_TOO_SMALL,
    GATEWAY_LINK_MALFORMED,
    GATEWAY_LINK_CRC_MISMATCH,
    GATEWAY_LINK_UNSUPPORTED_VERSION,
    GATEWAY_LINK_UNSUPPORTED_VALUE,
} gateway_link_result_t;

typedef enum {
    GATEWAY_LINK_MSG_HELLO = 0x01,
    GATEWAY_LINK_MSG_HELLO_ACK = 0x02,
    GATEWAY_LINK_MSG_PING = 0x03,
    GATEWAY_LINK_MSG_PONG = 0x04,
    GATEWAY_LINK_MSG_SNAPSHOT_REQUEST = 0x05,
    GATEWAY_LINK_MSG_SNAPSHOT_BEGIN = 0x06,
    GATEWAY_LINK_MSG_SNAPSHOT_END = 0x07,
    GATEWAY_LINK_MSG_INPUT_DESCRIPTOR = 0x10,
    GATEWAY_LINK_MSG_MEASUREMENT = 0x11,
    GATEWAY_LINK_MSG_SET_MEASUREMENT_POLICY = 0x20,
    GATEWAY_LINK_MSG_CONFIG_RESULT = 0x21,
    GATEWAY_LINK_MSG_PERMIT_JOIN = 0x22,
} gateway_link_message_type_t;

typedef enum {
    GATEWAY_LINK_ROLE_C6_GATEWAY = 1,
    GATEWAY_LINK_ROLE_S3_HOST = 2,
} gateway_link_role_t;

typedef enum {
    GATEWAY_LINK_QUALITY_VALID = 0,
    GATEWAY_LINK_QUALITY_STALE = 1,
    GATEWAY_LINK_QUALITY_ESTIMATED = 2,
    GATEWAY_LINK_QUALITY_INVALID = 3,
} gateway_link_quality_t;

typedef enum {
    GATEWAY_LINK_CONFIG_APPLIED = 0,
    GATEWAY_LINK_CONFIG_CLAMPED = 1,
    GATEWAY_LINK_CONFIG_UNSUPPORTED = 2,
    GATEWAY_LINK_CONFIG_ERROR = 3,
} gateway_link_config_status_t;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t sequence;
    uint16_t payload_length;
    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
} gateway_link_frame_t;

typedef struct {
    gateway_link_role_t role;
    uint8_t min_version;
    uint8_t max_version;
    uint16_t max_frame_bytes;
    uint32_t features;
} gateway_link_hello_t;

typedef struct {
    gateway_input_id_t input;
    bool available;
    gateway_input_capabilities_t capabilities;
    char model[GATEWAY_LINK_MODEL_MAX_BYTES];
} gateway_link_input_descriptor_t;

typedef struct {
    gateway_input_id_t input;
    uint32_t uptime_ms;
    gateway_measurement_t measurement;
    gateway_link_quality_t quality;
} gateway_link_measurement_t;

typedef struct {
    uint32_t request_id;
    gateway_input_id_t input;
    gateway_measurement_kind_t kind;
    uint32_t min_interval_ms;
    uint32_t max_interval_ms;
    double reportable_change;
} gateway_link_measurement_policy_t;

typedef struct {
    uint32_t request_id;
    gateway_link_config_status_t status;
} gateway_link_config_result_t;

typedef struct {
    uint32_t request_id;
    uint8_t duration_seconds;
} gateway_link_permit_join_t;

uint32_t gateway_link_crc32(const uint8_t *data, size_t length);

gateway_link_result_t gateway_link_encode_frame(
    const gateway_link_frame_t *frame,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_length);

gateway_link_result_t gateway_link_decode_frame(
    const uint8_t *encoded,
    size_t encoded_length,
    gateway_link_frame_t *frame);

gateway_link_result_t gateway_link_encode_hello_payload(
    const gateway_link_hello_t *hello,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_hello_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_hello_t *hello);

gateway_link_result_t gateway_link_encode_input_descriptor_payload(
    const gateway_link_input_descriptor_t *descriptor,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_input_descriptor_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_input_descriptor_t *descriptor);

gateway_link_result_t gateway_link_encode_measurement_payload(
    const gateway_link_measurement_t *measurement,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_measurement_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_measurement_t *measurement);

gateway_link_result_t gateway_link_encode_measurement_policy_payload(
    const gateway_link_measurement_policy_t *policy,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_measurement_policy_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_measurement_policy_t *policy);

gateway_link_result_t gateway_link_encode_config_result_payload(
    const gateway_link_config_result_t *result,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_config_result_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_config_result_t *result);

gateway_link_result_t gateway_link_encode_permit_join_payload(
    const gateway_link_permit_join_t *command,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_permit_join_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_permit_join_t *command);

gateway_link_result_t gateway_link_encode_u32_payload(
    uint32_t value,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length);

gateway_link_result_t gateway_link_decode_u32_payload(
    const uint8_t *payload,
    uint16_t length,
    uint32_t *value);
'''

source = r'''#include "gateway_link_protocol.h"

#include <string.h>

#define LINK_MAGIC_0 ((uint8_t)'G')
#define LINK_MAGIC_1 ((uint8_t)'L')
#define LINK_HEADER_BYTES 12U
#define LINK_CRC_BYTES 4U
#define LINK_RAW_MAX_BYTES (LINK_HEADER_BYTES + GATEWAY_LINK_MAX_PAYLOAD + LINK_CRC_BYTES)

_Static_assert(sizeof(double) == 8U, "GatewayLink v1 requires 64-bit IEEE-754 double");

static size_t bounded_string_length(const char *text, size_t limit)
{
    size_t length = 0U;
    if (text == NULL) {
        return 0U;
    }
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16_le(const uint8_t *in)
{
    return (uint16_t)in[0] | ((uint16_t)in[1] << 8U);
}

static void write_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
    out[2] = (uint8_t)(value >> 16U);
    out[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0] |
        ((uint32_t)in[1] << 8U) |
        ((uint32_t)in[2] << 16U) |
        ((uint32_t)in[3] << 24U);
}

static void write_u64_le(uint8_t *out, uint64_t value)
{
    for (uint8_t i = 0U; i < 8U; ++i) {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint64_t read_u64_le(const uint8_t *in)
{
    uint64_t value = 0U;
    for (uint8_t i = 0U; i < 8U; ++i) {
        value |= (uint64_t)in[i] << (8U * i);
    }
    return value;
}

static void write_double_le(uint8_t *out, double value)
{
    uint64_t bits = 0U;
    memcpy(&bits, &value, sizeof(bits));
    write_u64_le(out, bits);
}

static double read_double_le(const uint8_t *in)
{
    const uint64_t bits = read_u64_le(in);
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t gateway_link_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffUL;
    if (data == NULL && length != 0U) {
        return 0U;
    }
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320UL : 0U);
        }
    }
    return crc ^ 0xffffffffUL;
}

static gateway_link_result_t cobs_encode(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (input == NULL || output == NULL || output_length == NULL || output_capacity == 0U) {
        return GATEWAY_LINK_INVALID_ARG;
    }

    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if (write_index > output_capacity) {
        return GATEWAY_LINK_BUFFER_TOO_SMALL;
    }

    while (read_index < input_length) {
        if (input[read_index] == 0U) {
            output[code_index] = code;
            code_index = write_index;
            if (++write_index > output_capacity) {
                return GATEWAY_LINK_BUFFER_TOO_SMALL;
            }
            code = 1U;
            ++read_index;
            continue;
        }

        if (write_index >= output_capacity) {
            return GATEWAY_LINK_BUFFER_TOO_SMALL;
        }
        output[write_index++] = input[read_index++];
        ++code;

        if (code == 0xffU) {
            output[code_index] = code;
            code_index = write_index;
            if (++write_index > output_capacity) {
                return GATEWAY_LINK_BUFFER_TOO_SMALL;
            }
            code = 1U;
        }
    }

    output[code_index] = code;
    *output_length = write_index;
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t cobs_decode(
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (input == NULL || output == NULL || output_length == NULL || input_length == 0U) {
        return GATEWAY_LINK_INVALID_ARG;
    }

    size_t read_index = 0U;
    size_t write_index = 0U;
    while (read_index < input_length) {
        const uint8_t code = input[read_index++];
        if (code == 0U) {
            return GATEWAY_LINK_MALFORMED;
        }
        const size_t copy_count = (size_t)code - 1U;
        if (read_index + copy_count > input_length) {
            return GATEWAY_LINK_MALFORMED;
        }
        if (write_index + copy_count > output_capacity) {
            return GATEWAY_LINK_BUFFER_TOO_SMALL;
        }
        for (size_t i = 0U; i < copy_count; ++i) {
            output[write_index++] = input[read_index++];
        }
        if (code != 0xffU && read_index < input_length) {
            if (write_index >= output_capacity) {
                return GATEWAY_LINK_BUFFER_TOO_SMALL;
            }
            output[write_index++] = 0U;
        }
    }
    *output_length = write_index;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_frame(
    const gateway_link_frame_t *frame,
    uint8_t *encoded,
    size_t encoded_capacity,
    size_t *encoded_length)
{
    if (frame == NULL || encoded == NULL || encoded_length == NULL) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    if (frame->payload_length > GATEWAY_LINK_MAX_PAYLOAD) {
        return GATEWAY_LINK_INVALID_ARG;
    }

    uint8_t raw[LINK_RAW_MAX_BYTES];
    raw[0] = LINK_MAGIC_0;
    raw[1] = LINK_MAGIC_1;
    raw[2] = GATEWAY_LINK_PROTOCOL_VERSION;
    raw[3] = frame->type;
    raw[4] = frame->flags;
    raw[5] = 0U;
    write_u32_le(&raw[6], frame->sequence);
    write_u16_le(&raw[10], frame->payload_length);
    if (frame->payload_length != 0U) {
        memcpy(&raw[LINK_HEADER_BYTES], frame->payload, frame->payload_length);
    }
    const size_t crc_offset = LINK_HEADER_BYTES + frame->payload_length;
    write_u32_le(&raw[crc_offset], gateway_link_crc32(raw, crc_offset));
    const size_t raw_length = crc_offset + LINK_CRC_BYTES;

    if (encoded_capacity < 2U) {
        return GATEWAY_LINK_BUFFER_TOO_SMALL;
    }
    size_t cobs_length = 0U;
    const gateway_link_result_t result = cobs_encode(
        raw, raw_length, encoded, encoded_capacity - 1U, &cobs_length);
    if (result != GATEWAY_LINK_OK) {
        return result;
    }
    if (cobs_length + 1U > encoded_capacity || cobs_length + 1U > GATEWAY_LINK_MAX_FRAME_BYTES) {
        return GATEWAY_LINK_BUFFER_TOO_SMALL;
    }
    encoded[cobs_length] = 0U;
    *encoded_length = cobs_length + 1U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_frame(
    const uint8_t *encoded,
    size_t encoded_length,
    gateway_link_frame_t *frame)
{
    if (encoded == NULL || frame == NULL || encoded_length == 0U || encoded_length > GATEWAY_LINK_MAX_FRAME_BYTES) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    if (encoded[encoded_length - 1U] == 0U) {
        --encoded_length;
    }
    if (encoded_length == 0U) {
        return GATEWAY_LINK_MALFORMED;
    }

    uint8_t raw[LINK_RAW_MAX_BYTES];
    size_t raw_length = 0U;
    gateway_link_result_t result = cobs_decode(encoded, encoded_length, raw, sizeof(raw), &raw_length);
    if (result != GATEWAY_LINK_OK) {
        return result;
    }
    if (raw_length < LINK_HEADER_BYTES + LINK_CRC_BYTES) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (raw[0] != LINK_MAGIC_0 || raw[1] != LINK_MAGIC_1 || raw[5] != 0U) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (raw[2] != GATEWAY_LINK_PROTOCOL_VERSION) {
        return GATEWAY_LINK_UNSUPPORTED_VERSION;
    }

    const uint16_t payload_length = read_u16_le(&raw[10]);
    if (payload_length > GATEWAY_LINK_MAX_PAYLOAD ||
        raw_length != LINK_HEADER_BYTES + (size_t)payload_length + LINK_CRC_BYTES) {
        return GATEWAY_LINK_MALFORMED;
    }
    const size_t crc_offset = LINK_HEADER_BYTES + payload_length;
    if (read_u32_le(&raw[crc_offset]) != gateway_link_crc32(raw, crc_offset)) {
        return GATEWAY_LINK_CRC_MISMATCH;
    }

    frame->type = raw[3];
    frame->flags = raw[4];
    frame->sequence = read_u32_le(&raw[6]);
    frame->payload_length = payload_length;
    if (payload_length != 0U) {
        memcpy(frame->payload, &raw[LINK_HEADER_BYTES], payload_length);
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t source_to_wire(gateway_source_t source, uint8_t *wire)
{
    if (wire == NULL) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    switch (source) {
    case GATEWAY_SOURCE_ZIGBEE: *wire = 1U; return GATEWAY_LINK_OK;
    case GATEWAY_SOURCE_LOCAL_I2C: *wire = 2U; return GATEWAY_LINK_OK;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
}

static gateway_link_result_t source_from_wire(uint8_t wire, gateway_source_t *source)
{
    if (source == NULL) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    switch (wire) {
    case 1U: *source = GATEWAY_SOURCE_ZIGBEE; return GATEWAY_LINK_OK;
    case 2U: *source = GATEWAY_SOURCE_LOCAL_I2C; return GATEWAY_LINK_OK;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
}

static gateway_link_result_t measurement_kind_to_wire(gateway_measurement_kind_t kind, uint8_t *wire)
{
    if (wire == NULL) return GATEWAY_LINK_INVALID_ARG;
    switch (kind) {
    case GATEWAY_MEAS_TEMPERATURE: *wire = 1U; break;
    case GATEWAY_MEAS_HUMIDITY: *wire = 2U; break;
    case GATEWAY_MEAS_ILLUMINANCE: *wire = 3U; break;
    case GATEWAY_MEAS_OCCUPANCY: *wire = 4U; break;
    case GATEWAY_MEAS_CO2: *wire = 5U; break;
    case GATEWAY_MEAS_BATTERY_VOLTAGE: *wire = 6U; break;
    case GATEWAY_MEAS_BATTERY_PERCENT: *wire = 7U; break;
    case GATEWAY_MEAS_MAINS_VOLTAGE: *wire = 8U; break;
    case GATEWAY_MEAS_VOLTAGE: *wire = 9U; break;
    case GATEWAY_MEAS_CURRENT: *wire = 10U; break;
    case GATEWAY_MEAS_POWER: *wire = 11U; break;
    case GATEWAY_MEAS_ENERGY: *wire = 12U; break;
    case GATEWAY_MEAS_ON_OFF: *wire = 13U; break;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t measurement_kind_from_wire(uint8_t wire, gateway_measurement_kind_t *kind)
{
    if (kind == NULL) return GATEWAY_LINK_INVALID_ARG;
    switch (wire) {
    case 1U: *kind = GATEWAY_MEAS_TEMPERATURE; break;
    case 2U: *kind = GATEWAY_MEAS_HUMIDITY; break;
    case 3U: *kind = GATEWAY_MEAS_ILLUMINANCE; break;
    case 4U: *kind = GATEWAY_MEAS_OCCUPANCY; break;
    case 5U: *kind = GATEWAY_MEAS_CO2; break;
    case 6U: *kind = GATEWAY_MEAS_BATTERY_VOLTAGE; break;
    case 7U: *kind = GATEWAY_MEAS_BATTERY_PERCENT; break;
    case 8U: *kind = GATEWAY_MEAS_MAINS_VOLTAGE; break;
    case 9U: *kind = GATEWAY_MEAS_VOLTAGE; break;
    case 10U: *kind = GATEWAY_MEAS_CURRENT; break;
    case 11U: *kind = GATEWAY_MEAS_POWER; break;
    case 12U: *kind = GATEWAY_MEAS_ENERGY; break;
    case 13U: *kind = GATEWAY_MEAS_ON_OFF; break;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t unit_to_wire(gateway_unit_t unit, uint8_t *wire)
{
    if (wire == NULL) return GATEWAY_LINK_INVALID_ARG;
    switch (unit) {
    case GATEWAY_UNIT_NONE: *wire = 0U; break;
    case GATEWAY_UNIT_CELSIUS: *wire = 1U; break;
    case GATEWAY_UNIT_PERCENT: *wire = 2U; break;
    case GATEWAY_UNIT_LUX_LOG: *wire = 3U; break;
    case GATEWAY_UNIT_PPM: *wire = 4U; break;
    case GATEWAY_UNIT_VOLTS: *wire = 5U; break;
    case GATEWAY_UNIT_AMPS: *wire = 6U; break;
    case GATEWAY_UNIT_WATTS: *wire = 7U; break;
    case GATEWAY_UNIT_KILOWATT_HOURS: *wire = 8U; break;
    case GATEWAY_UNIT_BOOLEAN: *wire = 9U; break;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t unit_from_wire(uint8_t wire, gateway_unit_t *unit)
{
    if (unit == NULL) return GATEWAY_LINK_INVALID_ARG;
    switch (wire) {
    case 0U: *unit = GATEWAY_UNIT_NONE; break;
    case 1U: *unit = GATEWAY_UNIT_CELSIUS; break;
    case 2U: *unit = GATEWAY_UNIT_PERCENT; break;
    case 3U: *unit = GATEWAY_UNIT_LUX_LOG; break;
    case 4U: *unit = GATEWAY_UNIT_PPM; break;
    case 5U: *unit = GATEWAY_UNIT_VOLTS; break;
    case 6U: *unit = GATEWAY_UNIT_AMPS; break;
    case 7U: *unit = GATEWAY_UNIT_WATTS; break;
    case 8U: *unit = GATEWAY_UNIT_KILOWATT_HOURS; break;
    case 9U: *unit = GATEWAY_UNIT_BOOLEAN; break;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t encode_input_ref(
    const gateway_input_id_t *input,
    uint8_t *out,
    size_t capacity,
    size_t *used)
{
    if (input == NULL || out == NULL || used == NULL) return GATEWAY_LINK_INVALID_ARG;
    const size_t id_length = bounded_string_length(input->id, GATEWAY_INPUT_ID_MAX_BYTES);
    if (id_length == 0U || id_length >= GATEWAY_INPUT_ID_MAX_BYTES || id_length > 255U) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    if (capacity < 3U + id_length) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    gateway_link_result_t result = source_to_wire(input->source, &out[0]);
    if (result != GATEWAY_LINK_OK) return result;
    out[1] = input->channel;
    out[2] = (uint8_t)id_length;
    memcpy(&out[3], input->id, id_length);
    *used = 3U + id_length;
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t decode_input_ref(
    const uint8_t *in,
    size_t length,
    gateway_input_id_t *input,
    size_t *used)
{
    if (in == NULL || input == NULL || used == NULL || length < 3U) return GATEWAY_LINK_INVALID_ARG;
    const uint8_t id_length = in[2];
    if (id_length == 0U || id_length >= GATEWAY_INPUT_ID_MAX_BYTES || length < 3U + id_length) {
        return GATEWAY_LINK_MALFORMED;
    }
    gateway_link_result_t result = source_from_wire(in[0], &input->source);
    if (result != GATEWAY_LINK_OK) return result;
    input->channel = in[1];
    memcpy(input->id, &in[3], id_length);
    input->id[id_length] = '\0';
    *used = 3U + id_length;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_hello_payload(
    const gateway_link_hello_t *hello,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (hello == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 9U || hello->role < GATEWAY_LINK_ROLE_C6_GATEWAY || hello->role > GATEWAY_LINK_ROLE_S3_HOST) {
        return capacity < 9U ? GATEWAY_LINK_BUFFER_TOO_SMALL : GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    payload[0] = (uint8_t)hello->role;
    payload[1] = hello->min_version;
    payload[2] = hello->max_version;
    write_u16_le(&payload[3], hello->max_frame_bytes);
    write_u32_le(&payload[5], hello->features);
    *length = 9U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_hello_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_hello_t *hello)
{
    if (payload == NULL || hello == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 9U) return GATEWAY_LINK_MALFORMED;
    if (payload[0] < GATEWAY_LINK_ROLE_C6_GATEWAY || payload[0] > GATEWAY_LINK_ROLE_S3_HOST) {
        return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    hello->role = (gateway_link_role_t)payload[0];
    hello->min_version = payload[1];
    hello->max_version = payload[2];
    hello->max_frame_bytes = read_u16_le(&payload[3]);
    hello->features = read_u32_le(&payload[5]);
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_input_descriptor_payload(
    const gateway_link_input_descriptor_t *descriptor,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (descriptor == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    size_t used = 0U;
    gateway_link_result_t result = encode_input_ref(&descriptor->input, payload, capacity, &used);
    if (result != GATEWAY_LINK_OK) return result;
    const size_t model_length = bounded_string_length(descriptor->model, GATEWAY_LINK_MODEL_MAX_BYTES);
    if (model_length >= GATEWAY_LINK_MODEL_MAX_BYTES || model_length > 255U) return GATEWAY_LINK_INVALID_ARG;
    if (used + 6U + model_length > capacity) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    payload[used++] = descriptor->available ? 1U : 0U;
    write_u32_le(&payload[used], descriptor->capabilities);
    used += 4U;
    payload[used++] = (uint8_t)model_length;
    if (model_length != 0U) {
        memcpy(&payload[used], descriptor->model, model_length);
        used += model_length;
    }
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_input_descriptor_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_input_descriptor_t *descriptor)
{
    if (payload == NULL || descriptor == NULL) return GATEWAY_LINK_INVALID_ARG;
    size_t used = 0U;
    gateway_link_result_t result = decode_input_ref(payload, length, &descriptor->input, &used);
    if (result != GATEWAY_LINK_OK) return result;
    if (used + 6U > length) return GATEWAY_LINK_MALFORMED;
    if (payload[used] > 1U) return GATEWAY_LINK_MALFORMED;
    descriptor->available = payload[used++] != 0U;
    descriptor->capabilities = read_u32_le(&payload[used]);
    used += 4U;
    const uint8_t model_length = payload[used++];
    if (model_length >= GATEWAY_LINK_MODEL_MAX_BYTES || used + model_length != length) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (model_length != 0U) memcpy(descriptor->model, &payload[used], model_length);
    descriptor->model[model_length] = '\0';
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_measurement_payload(
    const gateway_link_measurement_t *measurement,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (measurement == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (measurement->quality > GATEWAY_LINK_QUALITY_INVALID) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    size_t used = 0U;
    gateway_link_result_t result = encode_input_ref(&measurement->input, payload, capacity, &used);
    if (result != GATEWAY_LINK_OK) return result;
    if (used + 15U > capacity) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    uint8_t kind = 0U;
    uint8_t unit = 0U;
    result = measurement_kind_to_wire(measurement->measurement.kind, &kind);
    if (result != GATEWAY_LINK_OK) return result;
    result = unit_to_wire(measurement->measurement.unit, &unit);
    if (result != GATEWAY_LINK_OK) return result;
    write_u32_le(&payload[used], measurement->uptime_ms);
    used += 4U;
    payload[used++] = kind;
    payload[used++] = unit;
    payload[used++] = (uint8_t)measurement->quality;
    write_double_le(&payload[used], measurement->measurement.value);
    used += 8U;
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_measurement_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_measurement_t *measurement)
{
    if (payload == NULL || measurement == NULL) return GATEWAY_LINK_INVALID_ARG;
    size_t used = 0U;
    gateway_link_result_t result = decode_input_ref(payload, length, &measurement->input, &used);
    if (result != GATEWAY_LINK_OK) return result;
    if (used + 15U != length) return GATEWAY_LINK_MALFORMED;
    measurement->uptime_ms = read_u32_le(&payload[used]);
    used += 4U;
    result = measurement_kind_from_wire(payload[used++], &measurement->measurement.kind);
    if (result != GATEWAY_LINK_OK) return result;
    result = unit_from_wire(payload[used++], &measurement->measurement.unit);
    if (result != GATEWAY_LINK_OK) return result;
    if (payload[used] > GATEWAY_LINK_QUALITY_INVALID) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    measurement->quality = (gateway_link_quality_t)payload[used++];
    measurement->measurement.value = read_double_le(&payload[used]);
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_measurement_policy_payload(
    const gateway_link_measurement_policy_t *policy,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (policy == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    size_t used = 0U;
    if (capacity < 4U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    write_u32_le(payload, policy->request_id);
    used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = encode_input_ref(&policy->input, &payload[used], capacity - used, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 17U > capacity) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    uint8_t kind = 0U;
    result = measurement_kind_to_wire(policy->kind, &kind);
    if (result != GATEWAY_LINK_OK) return result;
    payload[used++] = kind;
    write_u32_le(&payload[used], policy->min_interval_ms);
    used += 4U;
    write_u32_le(&payload[used], policy->max_interval_ms);
    used += 4U;
    write_double_le(&payload[used], policy->reportable_change);
    used += 8U;
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_measurement_policy_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_measurement_policy_t *policy)
{
    if (payload == NULL || policy == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length < 4U) return GATEWAY_LINK_MALFORMED;
    policy->request_id = read_u32_le(payload);
    size_t used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = decode_input_ref(&payload[used], length - used, &policy->input, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 17U != length) return GATEWAY_LINK_MALFORMED;
    result = measurement_kind_from_wire(payload[used++], &policy->kind);
    if (result != GATEWAY_LINK_OK) return result;
    policy->min_interval_ms = read_u32_le(&payload[used]);
    used += 4U;
    policy->max_interval_ms = read_u32_le(&payload[used]);
    used += 4U;
    policy->reportable_change = read_double_le(&payload[used]);
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_config_result_payload(
    const gateway_link_config_result_t *result,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (result == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 5U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    if (result->status > GATEWAY_LINK_CONFIG_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    write_u32_le(payload, result->request_id);
    payload[4] = (uint8_t)result->status;
    *length = 5U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_config_result_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_config_result_t *result)
{
    if (payload == NULL || result == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 5U) return GATEWAY_LINK_MALFORMED;
    if (payload[4] > GATEWAY_LINK_CONFIG_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    result->request_id = read_u32_le(payload);
    result->status = (gateway_link_config_status_t)payload[4];
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_permit_join_payload(
    const gateway_link_permit_join_t *command,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (command == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 5U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    write_u32_le(payload, command->request_id);
    payload[4] = command->duration_seconds;
    *length = 5U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_permit_join_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_permit_join_t *command)
{
    if (payload == NULL || command == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 5U) return GATEWAY_LINK_MALFORMED;
    command->request_id = read_u32_le(payload);
    command->duration_seconds = payload[4];
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_u32_payload(
    uint32_t value,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 4U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    write_u32_le(payload, value);
    *length = 4U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_u32_payload(
    const uint8_t *payload,
    uint16_t length,
    uint32_t *value)
{
    if (payload == NULL || value == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 4U) return GATEWAY_LINK_MALFORMED;
    *value = read_u32_le(payload);
    return GATEWAY_LINK_OK;
}
'''

test = r'''#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gateway_link_protocol.h"

static gateway_input_id_t scd41_input(void)
{
    gateway_input_id_t input = {0};
    input.source = GATEWAY_SOURCE_LOCAL_I2C;
    input.channel = 0U;
    strcpy(input.id, "scd4x:a12bef073b43");
    return input;
}

static void test_crc_known_vector(void)
{
    static const uint8_t text[] = "123456789";
    assert(gateway_link_crc32(text, 9U) == 0xcbf43926UL);
}

static void test_frame_round_trip_and_cobs_zero_safety(void)
{
    gateway_link_frame_t frame = {0};
    frame.type = GATEWAY_LINK_MSG_MEASUREMENT;
    frame.flags = 0x5aU;
    frame.sequence = 0x01020304UL;
    frame.payload_length = 7U;
    const uint8_t source[] = {0x11U, 0x00U, 0x22U, 0x00U, 0x00U, 0x33U, 0x44U};
    memcpy(frame.payload, source, sizeof(source));

    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_OK);
    assert(encoded_length <= GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(encoded[encoded_length - 1U] == 0U);
    for (size_t i = 0U; i + 1U < encoded_length; ++i) assert(encoded[i] != 0U);

    gateway_link_frame_t decoded = {0};
    assert(gateway_link_decode_frame(encoded, encoded_length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.type == frame.type);
    assert(decoded.flags == frame.flags);
    assert(decoded.sequence == frame.sequence);
    assert(decoded.payload_length == frame.payload_length);
    assert(memcmp(decoded.payload, source, sizeof(source)) == 0);
}

static void test_frame_crc_rejects_corruption(void)
{
    gateway_link_frame_t frame = {0};
    frame.type = GATEWAY_LINK_MSG_PING;
    frame.sequence = 7U;
    frame.payload_length = 4U;
    frame.payload[0] = 1U;
    frame.payload[1] = 2U;
    frame.payload[2] = 3U;
    frame.payload[3] = 4U;

    uint8_t encoded[GATEWAY_LINK_MAX_FRAME_BYTES];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_OK);
    assert(encoded_length > 8U);
    encoded[7] ^= 0x40U;

    gateway_link_frame_t decoded = {0};
    const gateway_link_result_t result = gateway_link_decode_frame(encoded, encoded_length, &decoded);
    assert(result == GATEWAY_LINK_CRC_MISMATCH || result == GATEWAY_LINK_MALFORMED);
}

static void test_hello_round_trip(void)
{
    const gateway_link_hello_t hello = {
        .role = GATEWAY_LINK_ROLE_C6_GATEWAY,
        .min_version = 1U,
        .max_version = 1U,
        .max_frame_bytes = GATEWAY_LINK_MAX_FRAME_BYTES,
        .features = GATEWAY_LINK_FEATURE_SNAPSHOT |
            GATEWAY_LINK_FEATURE_MEASUREMENT_POLICY |
            GATEWAY_LINK_FEATURE_PERMIT_JOIN,
    };
    uint8_t payload[32];
    uint16_t length = 0U;
    assert(gateway_link_encode_hello_payload(&hello, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);
    gateway_link_hello_t decoded = {0};
    assert(gateway_link_decode_hello_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.role == hello.role);
    assert(decoded.min_version == 1U && decoded.max_version == 1U);
    assert(decoded.max_frame_bytes == GATEWAY_LINK_MAX_FRAME_BYTES);
    assert(decoded.features == hello.features);
}

static void test_scd41_descriptor_round_trip(void)
{
    gateway_link_input_descriptor_t descriptor = {0};
    descriptor.input = scd41_input();
    descriptor.available = true;
    descriptor.capabilities = GATEWAY_INPUT_CAP_TEMPERATURE |
        GATEWAY_INPUT_CAP_HUMIDITY |
        GATEWAY_INPUT_CAP_CO2;
    strcpy(descriptor.model, "SCD41");

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_input_descriptor_payload(
        &descriptor, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_input_descriptor_t decoded = {0};
    assert(gateway_link_decode_input_descriptor_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.input.source == GATEWAY_SOURCE_LOCAL_I2C);
    assert(decoded.input.channel == 0U);
    assert(strcmp(decoded.input.id, "scd4x:a12bef073b43") == 0);
    assert(decoded.available);
    assert(decoded.capabilities == 0x13U);
    assert(strcmp(decoded.model, "SCD41") == 0);
}

static void test_scd41_measurement_round_trip(void)
{
    gateway_link_measurement_t measurement = {0};
    measurement.input = scd41_input();
    measurement.uptime_ms = 5887U;
    measurement.measurement.kind = GATEWAY_MEAS_CO2;
    measurement.measurement.unit = GATEWAY_UNIT_PPM;
    measurement.measurement.value = 1123.0;
    measurement.quality = GATEWAY_LINK_QUALITY_VALID;

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_measurement_payload(
        &measurement, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_measurement_t decoded = {0};
    assert(gateway_link_decode_measurement_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(strcmp(decoded.input.id, measurement.input.id) == 0);
    assert(decoded.uptime_ms == 5887U);
    assert(decoded.measurement.kind == GATEWAY_MEAS_CO2);
    assert(decoded.measurement.unit == GATEWAY_UNIT_PPM);
    assert(fabs(decoded.measurement.value - 1123.0) < 0.000001);
    assert(decoded.quality == GATEWAY_LINK_QUALITY_VALID);
}

static void test_measurement_policy_round_trip(void)
{
    gateway_link_measurement_policy_t policy = {0};
    policy.request_id = 0x11223344UL;
    policy.input = scd41_input();
    policy.kind = GATEWAY_MEAS_TEMPERATURE;
    policy.min_interval_ms = 5000U;
    policy.max_interval_ms = 60000U;
    policy.reportable_change = 0.2;

    uint8_t payload[GATEWAY_LINK_MAX_PAYLOAD];
    uint16_t length = 0U;
    assert(gateway_link_encode_measurement_policy_payload(
        &policy, payload, sizeof(payload), &length) == GATEWAY_LINK_OK);

    gateway_link_measurement_policy_t decoded = {0};
    assert(gateway_link_decode_measurement_policy_payload(payload, length, &decoded) == GATEWAY_LINK_OK);
    assert(decoded.request_id == policy.request_id);
    assert(strcmp(decoded.input.id, policy.input.id) == 0);
    assert(decoded.kind == GATEWAY_MEAS_TEMPERATURE);
    assert(decoded.min_interval_ms == 5000U);
    assert(decoded.max_interval_ms == 60000U);
    assert(fabs(decoded.reportable_change - 0.2) < 0.000001);
}

static void test_small_buffer_and_unknown_source_fail(void)
{
    gateway_link_input_descriptor_t descriptor = {0};
    descriptor.input = scd41_input();
    descriptor.input.source = (gateway_source_t)99;
    strcpy(descriptor.model, "SCD41");
    uint8_t payload[8];
    uint16_t length = 0U;
    assert(gateway_link_encode_input_descriptor_payload(
        &descriptor, payload, sizeof(payload), &length) != GATEWAY_LINK_OK);

    gateway_link_frame_t frame = {0};
    uint8_t encoded[4];
    size_t encoded_length = 0U;
    assert(gateway_link_encode_frame(&frame, encoded, sizeof(encoded), &encoded_length) == GATEWAY_LINK_BUFFER_TOO_SMALL);
}

int main(void)
{
    test_crc_known_vector();
    test_frame_round_trip_and_cobs_zero_safety();
    test_frame_crc_rejects_corruption();
    test_hello_round_trip();
    test_scd41_descriptor_round_trip();
    test_scd41_measurement_round_trip();
    test_measurement_policy_round_trip();
    test_small_buffer_and_unknown_source_fail();
    puts("gateway_link_protocol host tests passed");
    return 0;
}
'''

doc = r'''# GatewayLink v1

GatewayLink is the protocol-neutral MCU-to-MCU link between the ESP32-C6 input gateway and the ESP32-S3 application host. It intentionally transports normalized input identity, capabilities, availability and measurements rather than Zigbee clusters, I2C registers or driver-specific structures.

## Physical link reserved for the next stage

The intended C6 application UART is UART1 at 460800 baud, 8-N-1, no flow control, with C6 TX on GPIO18 and C6 RX on GPIO19. GPIO0/GPIO1 remain the local I2C SCL/SDA pair used by SCD4x. The v1 codec is hardware-independent; UART initialization is deliberately not part of this stage.

## Framing

Each on-wire frame is a COBS-encoded binary packet followed by one `0x00` delimiter. `0x00` cannot appear inside the COBS body, so a receiver can recover framing after lost or corrupt bytes by scanning for the next delimiter.

The decoded packet is little-endian:

| Field | Bytes | Notes |
| --- | ---: | --- |
| magic | 2 | ASCII `GL` |
| protocol_version | 1 | `1` |
| message_type | 1 | stable v1 wire value |
| flags | 1 | reserved for message semantics |
| reserved | 1 | must be zero in v1 |
| sequence | 4 | sender-local monotonically increasing sequence, wrap allowed |
| payload_length | 2 | 0..220 |
| payload | N | message-specific |
| CRC32 | 4 | IEEE CRC32 over header + payload |

Maximum encoded frame size is 256 bytes. The codec uses fixed-size buffers and performs no dynamic allocation.

## Stable input reference

Every input-bearing payload begins with:

| Field | Bytes | Notes |
| --- | ---: | --- |
| source | 1 | `1=Zigbee`, `2=local I2C` |
| channel | 1 | logical endpoint/channel |
| id_length | 1 | 1..39 |
| id | N | stable UTF-8/ASCII identifier, not NUL terminated on wire |

For example the validated local sensor is `source=2`, `channel=0`, `id=scd4x:a12bef073b43`. A Zigbee adapter should expose the authoritative IEEE-based identity before publishing it over GatewayLink; mutable short addresses are not application identities.

## Message types

- `0x01 HELLO`, `0x02 HELLO_ACK`: negotiate compatible protocol range, maximum frame size and features.
- `0x03 PING`, `0x04 PONG`: four-byte opaque token.
- `0x05 SNAPSHOT_REQUEST`, `0x06 SNAPSHOT_BEGIN`, `0x07 SNAPSHOT_END`: four-byte request/snapshot token. Input descriptors are sent between begin/end.
- `0x10 INPUT_DESCRIPTOR`: current availability, capability mask and model for one stable input.
- `0x11 MEASUREMENT`: normalized measurement for one stable input.
- `0x20 SET_MEASUREMENT_POLICY`: source-neutral reporting/publishing policy request.
- `0x21 CONFIG_RESULT`: request result (`APPLIED`, `CLAMPED`, `UNSUPPORTED`, `ERROR`).
- `0x22 PERMIT_JOIN`: Zigbee commissioning command intentionally kept at the gateway-control boundary rather than exposed as a measurement policy.

## HELLO payload

`role:u8, min_version:u8, max_version:u8, max_frame_bytes:u16, features:u32`.

Roles are `1=C6 gateway`, `2=S3 host`. Feature bits currently advertise snapshot, measurement-policy and permit-join support.

## INPUT_DESCRIPTOR payload

After the stable input reference:

`available:u8, capabilities:u32, model_length:u8, model:N`.

The S3 owns the application-facing input registry. Receiving another descriptor for an existing stable input updates that registry entry rather than creating a protocol-specific object.

## MEASUREMENT payload

After the stable input reference:

`uptime_ms:u32, kind:u8, unit:u8, quality:u8, value:f64`.

`value` is an IEEE-754 64-bit value serialized little-endian; it is not a raw C struct. Measurement and unit wire numbers are explicitly mapped by the codec and do not depend on compiler enum layout.

Quality values are `0=VALID`, `1=STALE`, `2=ESTIMATED`, `3=INVALID`.

## SET_MEASUREMENT_POLICY payload

`request_id:u32`, followed by the stable input reference, then:

`kind:u8, min_interval_ms:u32, max_interval_ms:u32, reportable_change:f64`.

This request is deliberately source-neutral. A local SCD4x adapter may implement it as publication filtering while a Zigbee adapter may translate supported fields into Configure Reporting. Unsupported policy must return `CONFIG_RESULT=UNSUPPORTED`; the transport must not guess protocol-specific behavior.

## CONFIG_RESULT payload

`request_id:u32, status:u8` where status is `0=APPLIED`, `1=CLAMPED`, `2=UNSUPPORTED`, `3=ERROR`.

## PERMIT_JOIN payload

`request_id:u32, duration_seconds:u8`. Duration zero closes joining; nonzero durations follow the coordinator policy limits.

## Ownership and recovery

The C6 owns physical input adapters, discovery and normalization. The S3 owns the current application input registry, freshness policy and LiteGraph-facing nodes. Neither side sends raw in-memory C structures.

After either MCU reconnects, the S3 requests a snapshot. The C6 sends descriptors for all currently known stable inputs between `SNAPSHOT_BEGIN` and `SNAPSHOT_END`, then continues with incremental descriptors and measurements. A lost frame is detected by sequence gaps and/or CRC; COBS provides delimiter-level resynchronization.
'''

(ROOT / 'main/gateway_link_protocol.h').write_text(header)
(ROOT / 'main/gateway_link_protocol.c').write_text(source)
(ROOT / 'tests/host/test_gateway_link_protocol.c').write_text(test)
(ROOT / 'docs/GATEWAY_LINK_V1.md').write_text(doc)

cmake = (ROOT / 'main/CMakeLists.txt').read_text()
needle = '        "gateway_inputs.c"\n'
assert needle in cmake
cmake = cmake.replace(needle, needle + '        "gateway_link_protocol.c"\n', 1)
(ROOT / 'main/CMakeLists.txt').write_text(cmake)

workflow_path = ROOT / '.github/workflows/quality.yml'
workflow = workflow_path.read_text()
marker = '  host-reporting-policy:\n'
assert marker in workflow
job = '''  host-link-protocol:\n    runs-on: ubuntu-latest\n    steps:\n      - uses: actions/checkout@v4\n      - name: Build GatewayLink protocol host tests\n        run: |\n          cc -std=c11 -Wall -Wextra -Werror -pedantic \\\n            -Imain \\\n            tests/host/test_gateway_link_protocol.c \\\n            main/gateway_link_protocol.c \\\n            -lm \\\n            -o /tmp/test_gateway_link_protocol\n      - name: Run GatewayLink protocol host tests\n        run: /tmp/test_gateway_link_protocol\n\n'''
workflow = workflow.replace(marker, job + marker, 1)
workflow_path.write_text(workflow)

arch_path = ROOT / 'docs/ARCHITECTURE.md'
arch = arch_path.read_text()
needle = '- `gateway_transport.c/.h` consumes normalized events and renders the current serial/log transport. It must not own Zigbee state or interpretation policy.\n'
assert needle in arch
addition = '- `gateway_link_protocol.c/.h` defines the hardware-independent GatewayLink v1 framing and payload codec for the future C6-to-S3 link. It uses COBS framing, CRC32, explicit little-endian wire values and fixed-size buffers; it never serializes raw C structs.\n'
arch = arch.replace(needle, needle + addition, 1)
needle2 = 'The event bus is the transport boundary. Input adapters normalize measurements before publishing them.'
assert needle2 in arch
arch = arch.replace(needle2, 'The event bus is the internal transport boundary. Input adapters normalize measurements before publishing them. GatewayLink is the external MCU boundary and serializes only the normalized contract. ' + needle2[len('The event bus is the transport boundary. '):], 1)
arch_path.write_text(arch)

readme_path = ROOT / 'README.md'
readme = readme_path.read_text()
anchor = '## Build and flash\n'
assert anchor in readme
section = '''## GatewayLink to ESP32-S3\n\nThe protocol-neutral C6-to-S3 contract is specified in [docs/GATEWAY_LINK_V1.md](docs/GATEWAY_LINK_V1.md). GatewayLink v1 uses bounded binary COBS frames with CRC32 and carries stable input identity, descriptors, normalized measurements, snapshots and source-neutral measurement policy requests. The codec is host-tested and hardware-independent; the UART1 GPIO18/GPIO19 driver is a separate integration stage.\n\n'''
readme = readme.replace(anchor, section + anchor, 1)
readme_path.write_text(readme)

for path in [
    ROOT / 'main/gateway_link_protocol.h',
    ROOT / 'main/gateway_link_protocol.c',
    ROOT / 'tests/host/test_gateway_link_protocol.c',
    ROOT / 'docs/GATEWAY_LINK_V1.md',
]:
    assert b'\x00' not in path.read_bytes()
