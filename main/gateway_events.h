#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_inputs.h"

#define GATEWAY_EVENT_QUEUE_DEPTH 16U
#define GATEWAY_RAW_ATTRIBUTE_MAX_BYTES 96U
#define GATEWAY_TEXT_MAX_BYTES 64U
#define GATEWAY_MAX_DESCRIPTOR_CLUSTERS 48U

typedef struct {
    uint16_t short_addr;
    uint8_t ieee[8];
    bool ieee_valid;
} gateway_device_id_t;

typedef enum {
    GATEWAY_EVENT_STACK_READY,
    GATEWAY_EVENT_NETWORK_FORMED,
    GATEWAY_EVENT_NETWORK_RESTORED,
    GATEWAY_EVENT_PERMIT_JOIN,
    GATEWAY_EVENT_DEVICE_ANNOUNCE,
    GATEWAY_EVENT_DEVICE_REJOIN,
    GATEWAY_EVENT_DEVICE_LEAVE_RESET,
    GATEWAY_EVENT_DEVICE_LEAVE_REJOIN,
    GATEWAY_EVENT_DEVICE_LEAVE_UNKNOWN,
    GATEWAY_EVENT_DEVICE_UPDATE,
    GATEWAY_EVENT_DEVICE_AUTHORIZED,
    GATEWAY_EVENT_DEVICE_UNAVAILABLE,
    GATEWAY_EVENT_DEVICE_CHECK_IN,
    GATEWAY_EVENT_BINDING,
    GATEWAY_EVENT_ENDPOINT,
    GATEWAY_EVENT_BASIC,
    GATEWAY_EVENT_REPORTING_CONFIG,
    GATEWAY_EVENT_COMMAND_RESULT,
    GATEWAY_EVENT_INPUT_AVAILABLE,
    GATEWAY_EVENT_INPUT_UNAVAILABLE,
    GATEWAY_EVENT_MEASUREMENT,
    GATEWAY_EVENT_RAW_ATTRIBUTE,
    GATEWAY_EVENT_WARNING,
} gateway_event_kind_t;

typedef enum {
    GATEWAY_EVENT_CONFIG_NONE = 0,
    GATEWAY_EVENT_CONFIG_APPLIED,
    GATEWAY_EVENT_CONFIG_CLAMPED,
    GATEWAY_EVENT_CONFIG_UNSUPPORTED,
    GATEWAY_EVENT_CONFIG_ERROR,
} gateway_event_config_result_t;


typedef enum {
    GATEWAY_EVENT_COMMAND_TRANSMITTED = 0,
    GATEWAY_EVENT_COMMAND_UNSUPPORTED,
    GATEWAY_EVENT_COMMAND_INVALID,
    GATEWAY_EVENT_COMMAND_ERROR,
} gateway_event_command_result_t;

typedef struct {
    uint16_t cluster_id;
    uint16_t attribute_id;
    uint8_t zcl_type;
    uint16_t original_length;
    uint16_t copied_length;
    bool truncated;
    uint8_t bytes[GATEWAY_RAW_ATTRIBUTE_MAX_BYTES];
} gateway_raw_attribute_t;

typedef struct {
    gateway_source_t source;
    gateway_event_kind_t kind;
    gateway_device_id_t device;
    gateway_input_id_t input;
    uint8_t endpoint;
    uint32_t uptime_ms;
    union {
        struct {
            uint8_t duration;
        } permit;
        struct {
            uint16_t old_short_addr;
            uint16_t new_short_addr;
        } rejoin;
        struct {
            uint8_t leave_type;
            bool record_retained;
        } leave;
        struct {
            uint8_t status;
            uint8_t tc_action;
        } device_update;
        struct {
            uint8_t type;
            uint8_t status;
        } authorization;
        struct {
            uint16_t profile_id;
            uint16_t device_id;
            uint8_t input_count;
            uint8_t input_copied;
            uint8_t output_count;
            uint8_t output_copied;
            uint16_t input_clusters[GATEWAY_MAX_DESCRIPTOR_CLUSTERS];
            uint16_t output_clusters[GATEWAY_MAX_DESCRIPTOR_CLUSTERS];
        } endpoint_desc;
        struct {
            gateway_input_capability_profile_t profile;
            char manufacturer[GATEWAY_INPUT_METADATA_MAX_BYTES];
            char model[GATEWAY_INPUT_METADATA_MAX_BYTES];
        } input_desc;
        gateway_measurement_t measurement;
        struct {
            uint16_t cluster_id;
            uint16_t attribute_id;
            uint8_t status;
            uint32_t request_id;
            gateway_event_config_result_t result;
        } reporting;
        struct {
            uint16_t cluster_id;
            uint8_t status;
        } binding;
        struct {
            uint32_t request_id;
            gateway_event_command_result_t result;
            uint8_t status;
            uint8_t tsn;
        } command;
        gateway_raw_attribute_t raw;
        struct {
            char key[16];
            char value[GATEWAY_TEXT_MAX_BYTES];
        } text;
    } data;
} gateway_event_t;

bool gateway_events_init(void);
gateway_event_t gateway_event_make(
    gateway_event_kind_t kind, const gateway_device_id_t *device);
gateway_event_t gateway_event_make_input(
    gateway_event_kind_t kind, const gateway_input_id_t *input);
bool gateway_event_warning(
    const gateway_device_id_t *device, const char *text);
bool gateway_event_warning_input(
    const gateway_input_id_t *input, const char *text);
bool gateway_event_publish(const gateway_event_t *event);
bool gateway_event_receive(gateway_event_t *event, uint32_t timeout_ticks);
uint32_t gateway_event_take_dropped(void);
uint32_t gateway_uptime_ms(void);
