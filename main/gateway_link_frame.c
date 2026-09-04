#include "gateway_link_protocol.h"

#include <string.h>

#define LINK_MAGIC_0 ((uint8_t)'G')
#define LINK_MAGIC_1 ((uint8_t)'L')
#define LINK_HEADER_BYTES 12U
#define LINK_CRC_BYTES 4U
#define LINK_RAW_MAX_BYTES \
    (LINK_HEADER_BYTES + GATEWAY_LINK_MAX_PAYLOAD + LINK_CRC_BYTES)

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
    if (input == NULL || output == NULL || output_length == NULL ||
        output_capacity == 0U) {
        return GATEWAY_LINK_INVALID_ARG;
    }

    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

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
    if (input == NULL || output == NULL || output_length == NULL ||
        input_length == 0U) {
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
    if (cobs_length + 1U > encoded_capacity ||
        cobs_length + 1U > GATEWAY_LINK_MAX_FRAME_BYTES) {
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
    if (encoded == NULL || frame == NULL || encoded_length == 0U ||
        encoded_length > GATEWAY_LINK_MAX_FRAME_BYTES) {
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
    gateway_link_result_t result = cobs_decode(
        encoded, encoded_length, raw, sizeof(raw), &raw_length);
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
