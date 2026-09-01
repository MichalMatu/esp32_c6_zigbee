#include <assert.h>
#include <stdio.h>

#include "gateway_device_state.h"

static const uint8_t IEEE_A[8] = {1, 2, 3, 4, 5, 6, 7, 8};
static const uint8_t IEEE_B[8] = {8, 7, 6, 5, 4, 3, 2, 1};

int main(void)
{
    device_slot_t *a = gateway_device_upsert(0x1234U, IEEE_A);
    assert(a != NULL);
    assert(a->state == SLOT_ACTIVE);
    assert(a->device.short_addr == 0x1234U);
    assert(gateway_device_find_by_ieee(IEEE_A, false) == a);
    assert(gateway_device_find_by_short(0x1234U, false) == a);

    const device_ref_t first_ref = gateway_device_ref_for(a);
    assert(gateway_device_from_ref(first_ref, false) == a);

    device_slot_t *same = gateway_device_upsert(0x2345U, IEEE_A);
    assert(same == a);
    assert(a->previous_short_addr == 0x1234U);
    assert(a->device.short_addr == 0x2345U);
    assert(gateway_device_find_by_short(0x1234U, false) == NULL);

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
    assert(gateway_device_endpoint_state(a2, 99U, true) == NULL);
    assert(gateway_device_endpoint_state(NULL, 1U, true) == NULL);

    puts("gateway_device_state host tests passed");
    return 0;
}
