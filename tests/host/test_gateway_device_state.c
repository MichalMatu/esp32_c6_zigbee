#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gateway_device_state.h"

static const uint8_t IEEE_A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t IEEE_B[8] = {8, 7, 6, 5, 4, 3, 2, 1};

static void test_config_state(device_slot_t *slot)
{
    binding_state_t *first_binding = NULL;
    for (uint16_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *state = gateway_device_binding_state(
            slot, 1U, (uint16_t)(0x1000U + i), true);
        assert(state != NULL);
        assert(state->last_status == GATEWAY_CONFIG_STATUS_UNKNOWN);
        if (i == 0U) {
            first_binding = state;
        }
    }
    assert(gateway_device_binding_state(slot, 1U, 0x1000U, false) == first_binding);
    assert(gateway_device_binding_state(slot, 1U, 0x2000U, true) == NULL);

    reporting_state_t *first_reporting = NULL;
    for (uint16_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *state = gateway_device_reporting_state(
            slot, 1U, 0x3000U, i, true);
        assert(state != NULL);
        assert(state->last_status == GATEWAY_CONFIG_STATUS_UNKNOWN);
        if (i == 0U) {
            first_reporting = state;
        }
    }
    assert(gateway_device_reporting_state(
        slot, 1U, 0x3000U, 0U, false) == first_reporting);
    assert(gateway_device_reporting_state(
        slot, 1U, 0x3000U, 0x4000U, true) == NULL);
}

static void test_basic_metadata(endpoint_state_t *state)
{
    assert(state != NULL);
    assert(gateway_device_endpoint_update_basic(state, "Acme", "Model A"));
    assert(strcmp(state->manufacturer, "Acme") == 0);
    assert(strcmp(state->model, "Model A") == 0);
    assert(!gateway_device_endpoint_update_basic(state, "Acme", "Model A"));
    assert(gateway_device_endpoint_update_basic(state, NULL, "Model B"));
    assert(strcmp(state->manufacturer, "Acme") == 0);
    assert(strcmp(state->model, "Model B") == 0);
    assert(gateway_device_endpoint_update_basic(
        state, "Manufacturer name longer than the bounded field", NULL));
    assert(strlen(state->manufacturer) == GATEWAY_BASIC_TEXT_MAX_BYTES - 1U);
}

int main(void)
{
    device_slot_t *a = gateway_device_upsert(0x1234U, IEEE_A);
    assert(a != NULL);
    assert(a->state == SLOT_ACTIVE);
    assert(a->device.short_addr == 0x1234U);
    assert(gateway_device_find_by_ieee(IEEE_A, false) == a);
    assert(gateway_device_find_by_short(0x1234U, false) == a);
    assert(gateway_device_route_is_current(a, 0x1234U));
    assert(!gateway_device_route_is_current(a, GATEWAY_INVALID_SHORT_ADDR));
    assert(a->discovery_short_addr == GATEWAY_INVALID_SHORT_ADDR);
    assert(gateway_device_claim_discovery(a));
    assert(a->discovery_short_addr == 0x1234U);
    assert(!gateway_device_claim_discovery(a));
    /* Repeated check-ins for one route must not claim duplicate discovery. */
    assert(!gateway_device_claim_discovery(a));
    gateway_device_release_discovery(a, 0x9999U);
    assert(a->discovery_short_addr == 0x1234U);
    gateway_device_release_discovery(a, 0x1234U);
    assert(a->discovery_short_addr == GATEWAY_INVALID_SHORT_ADDR);
    assert(gateway_device_claim_discovery(a));

    const device_ref_t first_ref = gateway_device_ref_for(a);
    assert(gateway_device_from_ref(first_ref, false) == a);

    device_slot_t *same = gateway_device_upsert(0x2345U, IEEE_A);
    assert(same == a);
    assert(a->previous_short_addr == 0x1234U);
    assert(a->device.short_addr == 0x2345U);
    assert(!gateway_device_route_is_current(a, 0x1234U));
    assert(gateway_device_route_is_current(a, 0x2345U));
    assert(gateway_device_find_by_short(0x1234U, false) == NULL);
    assert(gateway_device_claim_discovery(a));
    assert(a->discovery_short_addr == 0x2345U);
    gateway_device_reset_discovery(a);
    assert(a->discovery_short_addr == GATEWAY_INVALID_SHORT_ADDR);

    a->pending_requests = 1U;
    device_slot_t *b = gateway_device_upsert(0x2345U, IEEE_B);
    assert(b != NULL && b != a);
    assert(b->state == SLOT_ACTIVE);
    assert(a->state == SLOT_LEAVING);
    assert(a->device.short_addr == GATEWAY_INVALID_SHORT_ADDR);
    assert(gateway_device_find_by_ieee(IEEE_A, false) == NULL);
    assert(gateway_device_find_by_ieee(IEEE_A, true) == a);
    assert(gateway_device_find_by_short(0x2345U, false) == b);

    a->pending_requests = 0U;
    gateway_device_maybe_reclaim(a);
    assert(a->state == SLOT_EMPTY);
    assert(gateway_device_from_ref(first_ref, true) == NULL);

    device_slot_t *a2 = gateway_device_upsert(0x3456U, IEEE_A);
    assert(a2 != NULL);
    assert(a2->generation != first_ref.generation);
    assert(gateway_device_from_ref(first_ref, true) == NULL);

    for (uint8_t ep = 1U; ep <= GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++ep) {
        endpoint_state_t *state = gateway_device_endpoint_state(a2, ep, true);
        assert(state != NULL);
        assert(state->endpoint == ep);
        assert(gateway_device_endpoint_state(a2, ep, false) == state);
    }
    test_basic_metadata(gateway_device_endpoint_state(a2, 1U, false));
    test_config_state(a2);
    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);
    assert(gateway_device_endpoint_state(NULL, 1U, true) == NULL);

    puts("gateway_device_state host tests passed");
    return 0;
}
