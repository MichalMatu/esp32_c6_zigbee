#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "gateway_zcl_value.h"

#define CLUSTER_POWER_CONFIG 0x0001U
#define CLUSTER_ON_OFF 0x0006U
#define CLUSTER_ILLUMINANCE 0x0400U
#define CLUSTER_TEMPERATURE 0x0402U
#define CLUSTER_HUMIDITY 0x0405U
#define CLUSTER_OCCUPANCY 0x0406U
#define CLUSTER_CO2 0x040DU

#define TYPE_BOOL 0x10U
#define TYPE_UINT8 0x20U
#define TYPE_UINT16 0x21U
#define TYPE_INT16 0x29U
#define TYPE_SINGLE 0x39U

static void expect_close(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.0001);
}

static void test_temperature(void)
{
    int16_t raw = 2345;
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;
    assert(gateway_zcl_normalize(
        CLUSTER_TEMPERATURE, 0x0000U, TYPE_INT16, &raw, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_TEMPERATURE);
    assert(unit == GATEWAY_UNIT_CELSIUS);
    expect_close(value, 23.45);

    raw = INT16_MIN;
    assert(!gateway_zcl_normalize(
        CLUSTER_TEMPERATURE, 0x0000U, TYPE_INT16, &raw, &kind, &unit, &value));
}

static void test_humidity_and_battery(void)
{
    uint16_t humidity = 5432U;
    uint8_t battery = 173U;
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;

    assert(gateway_zcl_normalize(
        CLUSTER_HUMIDITY, 0x0000U, TYPE_UINT16, &humidity, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_HUMIDITY);
    expect_close(value, 54.32);

    assert(gateway_zcl_normalize(
        CLUSTER_POWER_CONFIG, 0x0021U, TYPE_UINT8, &battery, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_BATTERY_PERCENT);
    expect_close(value, 86.5);
}

static void test_boolean_values(void)
{
    uint8_t one = 1U;
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;

    assert(gateway_zcl_normalize(
        CLUSTER_ON_OFF, 0x0000U, TYPE_BOOL, &one, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_ON_OFF);
    assert(unit == GATEWAY_UNIT_BOOLEAN);
    expect_close(value, 1.0);

    assert(gateway_zcl_normalize(
        CLUSTER_OCCUPANCY, 0x0000U, TYPE_UINT8, &one, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_OCCUPANCY);
}

static void test_attribute_capabilities(void)
{
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0x0020U) ==
        GATEWAY_INPUT_CAP_BATTERY_VOLTAGE);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0x0021U) ==
        GATEWAY_INPUT_CAP_BATTERY_PERCENT);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_ON_OFF, 0x0000U) ==
        GATEWAY_INPUT_CAP_ON_OFF);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_TEMPERATURE, 0x0000U) ==
        GATEWAY_INPUT_CAP_TEMPERATURE);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_HUMIDITY, 0x0000U) ==
        GATEWAY_INPUT_CAP_HUMIDITY);
    assert(gateway_zcl_capability_for_attribute(CLUSTER_POWER_CONFIG, 0xffffU) == 0U);
}

static void test_co2_and_invalid_input(void)
{
    float co2_fraction = 0.0008f;
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;

    assert(gateway_zcl_normalize(
        CLUSTER_CO2, 0x0000U, TYPE_SINGLE, &co2_fraction, &kind, &unit, &value));
    assert(kind == GATEWAY_MEAS_CO2);
    expect_close(value, 800.0);

    co2_fraction = 2.0f;
    assert(!gateway_zcl_normalize(
        CLUSTER_CO2, 0x0000U, TYPE_SINGLE, &co2_fraction, &kind, &unit, &value));
    assert(!gateway_zcl_normalize(
        CLUSTER_ILLUMINANCE, 0x0000U, TYPE_UINT16, NULL, &kind, &unit, &value));
    assert(!gateway_zcl_normalize(
        CLUSTER_TEMPERATURE, 0x0000U, TYPE_INT16, &co2_fraction, NULL, &unit, &value));
}

int main(void)
{
    test_temperature();
    test_humidity_and_battery();
    test_boolean_values();
    test_attribute_capabilities();
    test_co2_and_invalid_input();
    puts("gateway_zcl_value host tests passed");
    return 0;
}
