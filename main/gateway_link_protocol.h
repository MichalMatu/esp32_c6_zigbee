#pragma once

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
