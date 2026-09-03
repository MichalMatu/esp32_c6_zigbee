#pragma once

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
    GATEWAY_MEAS_LEVEL,
} gateway_measurement_kind_t;

typedef enum {
    GATEWAY_COMMAND_SET_ON_OFF = 0,
    GATEWAY_COMMAND_SET_LEVEL = 1,
} gateway_command_kind_t;

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
#define GATEWAY_INPUT_CAP_LEVEL           (1UL << 13)

#define GATEWAY_INPUT_METADATA_MAX_BYTES 24U

typedef struct {
    gateway_input_capabilities_t readable;
    gateway_input_capabilities_t reportable;
    gateway_input_capabilities_t configurable;
    gateway_input_capabilities_t commandable;
} gateway_input_capability_profile_t;

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
