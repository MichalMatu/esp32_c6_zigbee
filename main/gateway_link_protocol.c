#include "gateway_link_protocol.h"

#include <string.h>

#define LINK_MAGIC_0 ((uint8_t)'G')
#define LINK_MAGIC_1 ((uint8_t)'L')
#define LINK_HEADER_BYTES 12U
#define LINK_CRC_BYTES 4U
#define LINK_RAW_MAX_BYTES (LINK_HEADER_BYTES + GATEWAY_LINK_MAX_PAYLOAD + LINK_CRC_BYTES)

_Static_assert(sizeof(double) == 8U, "GatewayLink v2 requires 64-bit IEEE-754 double");

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
    case GATEWAY_MEAS_LEVEL: *wire = 14U; break;
    case GATEWAY_MEAS_CONTACT_OPEN: *wire = 15U; break;
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
    case 14U: *kind = GATEWAY_MEAS_LEVEL; break;
    case 15U: *kind = GATEWAY_MEAS_CONTACT_OPEN; break;
    default: return GATEWAY_LINK_UNSUPPORTED_VALUE;
    }
    return GATEWAY_LINK_OK;
}

static gateway_link_result_t command_kind_to_wire(
    gateway_command_kind_t kind, uint8_t *wire)
{
    if (wire == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (kind == GATEWAY_COMMAND_SET_ON_OFF) { *wire = 0U; return GATEWAY_LINK_OK; }
    if (kind == GATEWAY_COMMAND_SET_LEVEL) { *wire = 1U; return GATEWAY_LINK_OK; }
    return GATEWAY_LINK_UNSUPPORTED_VALUE;
}

static gateway_link_result_t command_kind_from_wire(
    uint8_t wire, gateway_command_kind_t *kind)
{
    if (kind == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (wire == 0U) { *kind = GATEWAY_COMMAND_SET_ON_OFF; return GATEWAY_LINK_OK; }
    if (wire == 1U) { *kind = GATEWAY_COMMAND_SET_LEVEL; return GATEWAY_LINK_OK; }
    return GATEWAY_LINK_UNSUPPORTED_VALUE;
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

    const size_t manufacturer_length = bounded_string_length(
        descriptor->manufacturer, GATEWAY_LINK_MANUFACTURER_MAX_BYTES);
    const size_t model_length = bounded_string_length(
        descriptor->model, GATEWAY_LINK_MODEL_MAX_BYTES);
    if (manufacturer_length >= GATEWAY_LINK_MANUFACTURER_MAX_BYTES ||
        model_length >= GATEWAY_LINK_MODEL_MAX_BYTES ||
        manufacturer_length > 255U || model_length > 255U) {
        return GATEWAY_LINK_INVALID_ARG;
    }
    if (used + 19U + manufacturer_length + model_length > capacity) {
        return GATEWAY_LINK_BUFFER_TOO_SMALL;
    }

    payload[used++] = descriptor->available ? 1U : 0U;
    write_u32_le(&payload[used], descriptor->profile.readable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.reportable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.configurable);
    used += 4U;
    write_u32_le(&payload[used], descriptor->profile.commandable);
    used += 4U;

    payload[used++] = (uint8_t)manufacturer_length;
    if (manufacturer_length != 0U) {
        memcpy(&payload[used], descriptor->manufacturer, manufacturer_length);
        used += manufacturer_length;
    }
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
    memset(descriptor, 0, sizeof(*descriptor));
    size_t used = 0U;
    gateway_link_result_t result = decode_input_ref(payload, length, &descriptor->input, &used);
    if (result != GATEWAY_LINK_OK) return result;
    if (used + 19U > length) return GATEWAY_LINK_MALFORMED;
    if (payload[used] > 1U) return GATEWAY_LINK_MALFORMED;
    descriptor->available = payload[used++] != 0U;
    descriptor->profile.readable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.reportable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.configurable = read_u32_le(&payload[used]);
    used += 4U;
    descriptor->profile.commandable = read_u32_le(&payload[used]);
    used += 4U;

    const uint8_t manufacturer_length = payload[used++];
    if (manufacturer_length >= GATEWAY_LINK_MANUFACTURER_MAX_BYTES ||
        used + manufacturer_length + 1U > length) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (manufacturer_length != 0U) {
        memcpy(descriptor->manufacturer, &payload[used], manufacturer_length);
        used += manufacturer_length;
    }
    descriptor->manufacturer[manufacturer_length] = '\0';

    const uint8_t model_length = payload[used++];
    if (model_length >= GATEWAY_LINK_MODEL_MAX_BYTES || used + model_length != length) {
        return GATEWAY_LINK_MALFORMED;
    }
    if (model_length != 0U) {
        memcpy(descriptor->model, &payload[used], model_length);
    }
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

gateway_link_result_t gateway_link_encode_command_request_payload(
    const gateway_link_command_request_t *command,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (command == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 4U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    write_u32_le(payload, command->request_id);
    size_t used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = encode_input_ref(
        &command->input, &payload[used], capacity - used, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 13U > capacity) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    uint8_t kind = 0U;
    result = command_kind_to_wire(command->kind, &kind);
    if (result != GATEWAY_LINK_OK) return result;
    payload[used++] = kind;
    write_double_le(&payload[used], command->value);
    used += 8U;
    write_u32_le(&payload[used], command->transition_ms);
    used += 4U;
    *length = (uint16_t)used;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_command_request_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_request_t *command)
{
    if (payload == NULL || command == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length < 4U) return GATEWAY_LINK_MALFORMED;
    memset(command, 0, sizeof(*command));
    command->request_id = read_u32_le(payload);
    size_t used = 4U;
    size_t ref_used = 0U;
    gateway_link_result_t result = decode_input_ref(
        &payload[used], length - used, &command->input, &ref_used);
    if (result != GATEWAY_LINK_OK) return result;
    used += ref_used;
    if (used + 13U != length) return GATEWAY_LINK_MALFORMED;
    result = command_kind_from_wire(payload[used++], &command->kind);
    if (result != GATEWAY_LINK_OK) return result;
    command->value = read_double_le(&payload[used]);
    used += 8U;
    command->transition_ms = read_u32_le(&payload[used]);
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_encode_command_result_payload(
    const gateway_link_command_result_t *result,
    uint8_t *payload,
    size_t capacity,
    uint16_t *length)
{
    if (result == NULL || payload == NULL || length == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (capacity < 5U) return GATEWAY_LINK_BUFFER_TOO_SMALL;
    if (result->status > GATEWAY_LINK_COMMAND_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    write_u32_le(payload, result->request_id);
    payload[4] = (uint8_t)result->status;
    *length = 5U;
    return GATEWAY_LINK_OK;
}

gateway_link_result_t gateway_link_decode_command_result_payload(
    const uint8_t *payload,
    uint16_t length,
    gateway_link_command_result_t *result)
{
    if (payload == NULL || result == NULL) return GATEWAY_LINK_INVALID_ARG;
    if (length != 5U) return GATEWAY_LINK_MALFORMED;
    if (payload[4] > GATEWAY_LINK_COMMAND_ERROR) return GATEWAY_LINK_UNSUPPORTED_VALUE;
    result->request_id = read_u32_le(payload);
    result->status = (gateway_link_command_status_t)payload[4];
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
