#include "gateway_zcl_value.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#ifndef GATEWAY_ZCL_HOST_TEST
#include <ezbee/zcl/zcl_core.h>
#else
#define EZB_ZCL_CLUSTER_ID_POWER_CONFIG 0x0001U
#define EZB_ZCL_CLUSTER_ID_ON_OFF 0x0006U
#define EZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT 0x0400U
#define EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT 0x0402U
#define EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT 0x0405U
#define EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING 0x0406U
#define EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT 0x040DU
#define EZB_ZCL_ATTR_TYPE_BOOL 0x10U
#define EZB_ZCL_ATTR_TYPE_MAP8 0x18U
#define EZB_ZCL_ATTR_TYPE_UINT8 0x20U
#define EZB_ZCL_ATTR_TYPE_UINT16 0x21U
#define EZB_ZCL_ATTR_TYPE_INT16 0x29U
#define EZB_ZCL_ATTR_TYPE_SINGLE 0x39U
#endif

#define ZCL_ATTR_MEASURED_VALUE 0x0000U
#define ZCL_ATTR_OCCUPANCY 0x0000U
#define ZCL_ATTR_ON_OFF 0x0000U
#define ZCL_ATTR_BATTERY_VOLTAGE 0x0020U
#define ZCL_ATTR_BATTERY_PERCENT 0x0021U

uint16_t gateway_zcl_attr_size(uint8_t type, const void *value)
{
#ifdef GATEWAY_ZCL_HOST_TEST
    (void)type;
    (void)value;
    return 0U;
#else
    return value == NULL ? 0U :
        ezb_zcl_get_attr_value_size((ezb_zcl_attr_type_t)type, value);
#endif
}

static bool read_u8(const void *value, uint8_t type, uint8_t *out)
{
    if (value == NULL || out == NULL ||
        (type != EZB_ZCL_ATTR_TYPE_UINT8 &&
         type != EZB_ZCL_ATTR_TYPE_BOOL &&
         type != EZB_ZCL_ATTR_TYPE_MAP8)) {
        return false;
    }
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_u16(const void *value, uint8_t type, uint16_t *out)
{
    if (value == NULL || out == NULL || type != EZB_ZCL_ATTR_TYPE_UINT16) {
        return false;
    }
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_s16(const void *value, uint8_t type, int16_t *out)
{
    if (value == NULL || out == NULL || type != EZB_ZCL_ATTR_TYPE_INT16) {
        return false;
    }
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_float(const void *value, uint8_t type, float *out)
{
    if (value == NULL || out == NULL || type != EZB_ZCL_ATTR_TYPE_SINGLE) {
        return false;
    }
    memcpy(out, value, sizeof(*out));
    return true;
}

gateway_input_capabilities_t gateway_zcl_capabilities_for_server_cluster(
    uint16_t cluster)
{
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG) {
        return GATEWAY_INPUT_CAP_BATTERY_VOLTAGE |
            GATEWAY_INPUT_CAP_BATTERY_PERCENT;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF) {
        return GATEWAY_INPUT_CAP_ON_OFF;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_ILLUMINANCE;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_TEMPERATURE;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_HUMIDITY;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING) {
        return GATEWAY_INPUT_CAP_OCCUPANCY;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT) {
        return GATEWAY_INPUT_CAP_CO2;
    }
    return 0U;
}

bool gateway_zcl_normalize(uint16_t cluster,
                           uint16_t attribute,
                           uint8_t type,
                           const void *value,
                           gateway_measurement_kind_t *kind,
                           gateway_unit_t *unit,
                           double *number)
{
    if (kind == NULL || unit == NULL || number == NULL) {
        return false;
    }

    uint8_t u8;
    uint16_t u16;
    int16_t s16;
    float single;

    if (cluster == EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT &&
        attribute == ZCL_ATTR_MEASURED_VALUE &&
        read_s16(value, type, &s16) && s16 != INT16_MIN) {
        *kind = GATEWAY_MEAS_TEMPERATURE;
        *unit = GATEWAY_UNIT_CELSIUS;
        *number = (double)s16 / 100.0;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT &&
        attribute == ZCL_ATTR_MEASURED_VALUE &&
        read_u16(value, type, &u16) && u16 <= 10000U) {
        *kind = GATEWAY_MEAS_HUMIDITY;
        *unit = GATEWAY_UNIT_PERCENT;
        *number = (double)u16 / 100.0;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT &&
        attribute == ZCL_ATTR_MEASURED_VALUE &&
        read_u16(value, type, &u16) && u16 != UINT16_MAX) {
        *kind = GATEWAY_MEAS_ILLUMINANCE;
        *unit = GATEWAY_UNIT_LUX_LOG;
        *number = u16;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING &&
        attribute == ZCL_ATTR_OCCUPANCY && read_u8(value, type, &u8)) {
        *kind = GATEWAY_MEAS_OCCUPANCY;
        *unit = GATEWAY_UNIT_BOOLEAN;
        *number = (u8 & 1U) != 0U;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT &&
        attribute == ZCL_ATTR_MEASURED_VALUE &&
        read_float(value, type, &single) && isfinite(single) &&
        single >= 0.0f && single <= 1.0f) {
        *kind = GATEWAY_MEAS_CO2;
        *unit = GATEWAY_UNIT_PPM;
        *number = (double)single * 1000000.0;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG &&
        attribute == ZCL_ATTR_BATTERY_VOLTAGE &&
        read_u8(value, type, &u8) && u8 != UINT8_MAX) {
        *kind = GATEWAY_MEAS_BATTERY_VOLTAGE;
        *unit = GATEWAY_UNIT_VOLTS;
        *number = (double)u8 / 10.0;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG &&
        attribute == ZCL_ATTR_BATTERY_PERCENT &&
        read_u8(value, type, &u8) && u8 <= 200U) {
        *kind = GATEWAY_MEAS_BATTERY_PERCENT;
        *unit = GATEWAY_UNIT_PERCENT;
        *number = (double)u8 / 2.0;
        return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF &&
        read_u8(value, type, &u8) && u8 <= 1U) {
        *kind = GATEWAY_MEAS_ON_OFF;
        *unit = GATEWAY_UNIT_BOOLEAN;
        *number = u8 != 0U;
        return true;
    }
    return false;
}
