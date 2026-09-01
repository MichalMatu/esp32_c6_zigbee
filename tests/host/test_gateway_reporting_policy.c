#include <assert.h>
#include <stdio.h>

#include "gateway_reporting_policy.h"

#define CLUSTER_POWER_CONFIG 0x0001U
#define CLUSTER_POLL_CONTROL 0x0020U
#define CLUSTER_TEMPERATURE 0x0402U
#define CLUSTER_HUMIDITY 0x0405U

int main(void)
{
    gateway_reporting_spec_t spec;

    assert(gateway_reporting_policy_reporting_mask(CLUSTER_TEMPERATURE) != 0U);
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

    assert(gateway_reporting_policy_reporting_mask(CLUSTER_POLL_CONTROL) == 0U);
    assert(gateway_reporting_policy_binding_mask(CLUSTER_POLL_CONTROL) != 0U);
    assert(!gateway_reporting_policy_spec(CLUSTER_POLL_CONTROL, &spec));
    assert(gateway_reporting_policy_reporting_mask(0xffffU) == 0U);
    assert(gateway_reporting_policy_binding_mask(0xffffU) == 0U);
    assert(!gateway_reporting_policy_spec(0xffffU, &spec));
    assert(!gateway_reporting_policy_spec(CLUSTER_TEMPERATURE, NULL));

    puts("gateway_reporting_policy host tests passed");
    return 0;
}
