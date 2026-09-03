#include <assert.h>
#include <stdio.h>

#include "gateway_reporting_policy.h"

#define CLUSTER_POWER_CONFIG 0x0001U
#define CLUSTER_POLL_CONTROL 0x0020U
#define CLUSTER_TEMPERATURE 0x0402U
#define CLUSTER_HUMIDITY 0x0405U

static void test_default_policy(void)
{
    gateway_reporting_spec_t spec;
    assert(gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, &spec));
    assert(spec.attribute_id == 0x0000U);
    assert(spec.attribute_type == 0x29U);
    assert(spec.min_interval == 60U);
    assert(spec.max_interval == 300U);
    assert(spec.change_kind == GATEWAY_REPORTING_CHANGE_S16);
    assert(spec.reportable_change == 10);

    assert(gateway_reporting_policy_spec(CLUSTER_HUMIDITY, &spec));
    assert(spec.attribute_type == 0x21U);
    assert(spec.reportable_change == 100);

    assert(gateway_reporting_policy_spec(CLUSTER_POWER_CONFIG, &spec));
    assert(spec.attribute_id == 0x0021U);
    assert(spec.attribute_type == 0x20U);
    assert(spec.min_interval == 3600U);
    assert(spec.max_interval == 21600U);
    assert(spec.change_kind == GATEWAY_REPORTING_CHANGE_U8);
    assert(spec.reportable_change == 2);

    assert(gateway_reporting_policy_requires_binding(CLUSTER_TEMPERATURE));
    assert(gateway_reporting_policy_requires_binding(CLUSTER_POLL_CONTROL));
    assert(!gateway_reporting_policy_requires_binding(0xffffU));
    assert(!gateway_reporting_policy_spec(0xffffU, &spec));
    assert(!gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, NULL));
}

static void test_requested_policy_translation(void)
{
    gateway_reporting_plan_t plan;
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 60000U, 300000U, 0.10, &plan) ==
        GATEWAY_REPORTING_PLAN_OK);
    assert(plan.cluster_id == CLUSTER_TEMPERATURE);
    assert(plan.spec.min_interval == 60U && plan.spec.max_interval == 300U);
    assert(plan.spec.reportable_change == 10);
    assert(plan.effective_reportable_change == 0.10);
    assert(!plan.clamped);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_HUMIDITY, 60500U, 300900U, 1.006, &plan) ==
        GATEWAY_REPORTING_PLAN_CLAMPED);
    assert(plan.spec.min_interval == 61U);
    assert(plan.spec.max_interval == 300U);
    assert(plan.spec.reportable_change == 101);
    assert(plan.effective_min_interval_ms == 61000U);
    assert(plan.effective_max_interval_ms == 300000U);
    assert(plan.effective_reportable_change == 1.01);
    assert(plan.clamped);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_BATTERY_PERCENT, 3600000U, 21600000U, 1.0, &plan) ==
        GATEWAY_REPORTING_PLAN_OK);
    assert(plan.cluster_id == CLUSTER_POWER_CONFIG);
    assert(plan.spec.reportable_change == 2);

    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_CO2, 1000U, 5000U, 1.0, &plan) ==
        GATEWAY_REPORTING_PLAN_UNSUPPORTED);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 5000U, 1000U, 0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 1000U, 0U, 0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
    assert(gateway_reporting_policy_plan(
        GATEWAY_MEAS_TEMPERATURE, 1000U, 5000U, -0.1, &plan) ==
        GATEWAY_REPORTING_PLAN_INVALID);
}

int main(void)
{
    test_default_policy();
    test_requested_policy_translation();
    puts("gateway_reporting_policy host tests passed");
    return 0;
}
