#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GATEWAY_EVENT_QUEUE_DEPTH 16U
#define GATEWAY_RAW_ATTRIBUTE_MAX_BYTES 96U
#define GATEWAY_TEXT_MAX_BYTES 64U
#define GATEWAY_MAX_DESCRIPTOR_CLUSTERS 48U

typedef enum {
    GATEWAY_SOURCE_ZIGBEE,
} gateway_source_t;

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
    GATEWAY_EVENT_DEVICE_LEAVE,
    GATEWAY_EVENT_DEVICE_UPDATE,
    GATEWAY_EVENT_DEVICE_UNAVAILABLE,
    GATEWAY_EVENT_ENDPOINT,
    GATEWAY_EVENT_BASIC,
    GATEWAY_EVENT_REPORTING_CONFIG,
    GATEWAY_EVENT_MEASUREMENT,
    GATEWAY_EVENT_RAW_ATTRIBUTE,
    GATEWAY_EVENT_WARNING,
} gateway_event_kind_t;

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
    uint8_t endpoint;
    uint32_t uptime_ms;
    union {
        struct {
            uint8_t duration;
        } permit;
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
            gateway_measurement_kind_t kind;
            gateway_unit_t unit;
            double value;
            uint16_t cluster_id;
            uint16_t attribute_id;
            uint8_t zcl_type;
        } measurement;
        struct {
            uint16_t cluster_id;
            uint16_t attribute_id;
            uint8_t status;
        } reporting;
        gateway_raw_attribute_t raw;
        struct {
            char key[16];
            char value[GATEWAY_TEXT_MAX_BYTES];
        } text;
    } data;
} gateway_event_t;

void gateway_events_init(void);
bool gateway_event_publish(const gateway_event_t *event);
bool gateway_event_receive(gateway_event_t *event, uint32_t timeout_ticks);
uint32_t gateway_event_take_dropped(void);
uint32_t gateway_uptime_ms(void);
