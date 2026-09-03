#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "gateway_command_policy.h"

static void test_on_off_plan(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.cluster_id == 0x0006U);
    assert(plan.capability == GATEWAY_INPUT_CAP_ON_OFF);
    assert(plan.target_on);

    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 0.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(!plan.target_on);
}

static void test_level_plan(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 50.0, 1200U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.kind == GATEWAY_COMMAND_SET_LEVEL);
    assert(plan.cluster_id == 0x0008U);
    assert(plan.capability == GATEWAY_INPUT_CAP_LEVEL);
    assert(plan.level == 127U);
    assert(plan.transition_time == 12U);

    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 100.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_OK);
    assert(plan.level == 254U);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, -1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 101.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_LEVEL, 50.0, 150U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
}

static void test_invalid_on_off_values(void)
{
    gateway_command_plan_t plan;
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 0.5, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, NAN, 0U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 100U, &plan) == GATEWAY_COMMAND_PLAN_INVALID);
    assert(gateway_command_policy_plan(
        (gateway_command_kind_t)99, 1.0, 0U, &plan) == GATEWAY_COMMAND_PLAN_UNSUPPORTED);
    assert(gateway_command_policy_plan(
        GATEWAY_COMMAND_SET_ON_OFF, 1.0, 0U, NULL) == GATEWAY_COMMAND_PLAN_INVALID);
}

int main(void)
{
    test_on_off_plan();
    test_level_plan();
    test_invalid_on_off_values();
    puts("gateway_command_policy host tests passed");
    return 0;
}
