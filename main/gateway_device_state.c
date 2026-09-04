#include "gateway_device_state.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static device_slot_t s_devices[GATEWAY_MAX_DEVICES];

static bool ieee_matches(const device_slot_t *slot, const uint8_t ieee[8])
{
    return ieee != NULL && slot->device.ieee_valid &&
        memcmp(slot->device.ieee, ieee, sizeof(slot->device.ieee)) == 0;
}

device_slot_t *gateway_device_find_by_ieee(
    const uint8_t ieee[8], bool include_non_active)
{
    for (size_t i = 0; ieee != NULL && i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].state != SLOT_EMPTY &&
            (include_non_active || s_devices[i].state == SLOT_ACTIVE) &&
            ieee_matches(&s_devices[i], ieee)) {
            return &s_devices[i];
        }
    }
    return NULL;
}

device_slot_t *gateway_device_find_by_short(
    uint16_t short_addr, bool include_non_active)
{
    for (size_t i = 0; i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].state != SLOT_EMPTY &&
            (include_non_active || s_devices[i].state == SLOT_ACTIVE) &&
            s_devices[i].device.short_addr == short_addr) {
            return &s_devices[i];
        }
    }
    return NULL;
}

device_ref_t gateway_device_ref_for(const device_slot_t *slot)
{
    return (device_ref_t){
        .index = (uint8_t)(slot - s_devices),
        .generation = slot->generation,
    };
}

device_slot_t *gateway_device_from_ref(
    device_ref_t ref, bool include_non_active)
{
    if (ref.index >= GATEWAY_MAX_DEVICES) {
        return NULL;
    }
    device_slot_t *slot = &s_devices[ref.index];
    if (slot->state == SLOT_EMPTY || slot->generation != ref.generation ||
        (!include_non_active && slot->state != SLOT_ACTIVE)) {
        return NULL;
    }
    return slot;
}

void gateway_device_maybe_reclaim(device_slot_t *slot)
{
    if (slot != NULL && slot->state == SLOT_LEAVING &&
        slot->pending_jobs == 0U && slot->pending_requests == 0U) {
        memset(slot->bindings, 0, sizeof(slot->bindings));
        memset(slot->reporting, 0, sizeof(slot->reporting));
        memset(slot->endpoints, 0, sizeof(slot->endpoints));
        memset(&slot->device, 0, sizeof(slot->device));
        slot->device.short_addr = GATEWAY_INVALID_SHORT_ADDR;
        slot->state = SLOT_EMPTY;
    }
}

static device_slot_t *allocate_device(uint16_t short_addr)
{
    for (size_t i = 0; i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].state == SLOT_EMPTY) {
            const uint32_t generation =
                s_devices[i].generation == UINT32_MAX ? 1U :
                s_devices[i].generation + 1U;
            memset(&s_devices[i], 0, sizeof(s_devices[i]));
            s_devices[i].state = SLOT_ACTIVE;
            s_devices[i].generation = generation;
            s_devices[i].device.short_addr = short_addr;
            s_devices[i].previous_short_addr = GATEWAY_INVALID_SHORT_ADDR;
            s_devices[i].discovery_short_addr = GATEWAY_INVALID_SHORT_ADDR;
            return &s_devices[i];
        }
    }
    return NULL;
}

device_slot_t *gateway_device_upsert(
    uint16_t short_addr, const uint8_t ieee[8])
{
    device_slot_t *slot = ieee == NULL ?
        gateway_device_find_by_short(short_addr, false) :
        gateway_device_find_by_ieee(ieee, true);
    if (slot == NULL) {
        slot = allocate_device(short_addr);
    }
    if (slot == NULL) {
        return NULL;
    }

    device_slot_t *old = gateway_device_find_by_short(short_addr, true);
    if (old != NULL && old != slot) {
        old->device.short_addr = GATEWAY_INVALID_SHORT_ADDR;
        old->state = SLOT_LEAVING;
        gateway_device_maybe_reclaim(old);
    }

    slot->state = SLOT_ACTIVE;
    if (slot->device.short_addr != GATEWAY_INVALID_SHORT_ADDR &&
        slot->device.short_addr != short_addr) {
        slot->previous_short_addr = slot->device.short_addr;
    }
    slot->device.short_addr = short_addr;
    if (ieee != NULL) {
        memcpy(slot->device.ieee, ieee, sizeof(slot->device.ieee));
        slot->device.ieee_valid = true;
    }
    return slot;
}

bool gateway_device_claim_discovery(device_slot_t *slot)
{
    if (slot == NULL || slot->state != SLOT_ACTIVE ||
        slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR ||
        slot->discovery_short_addr == slot->device.short_addr) {
        return false;
    }
    slot->discovery_short_addr = slot->device.short_addr;
    return true;
}

bool gateway_device_route_is_current(
    const device_slot_t *slot, uint16_t short_addr)
{
    return slot != NULL && slot->state == SLOT_ACTIVE &&
        short_addr != GATEWAY_INVALID_SHORT_ADDR &&
        slot->device.short_addr == short_addr;
}

void gateway_device_release_discovery(device_slot_t *slot, uint16_t short_addr)
{
    if (slot != NULL && slot->discovery_short_addr == short_addr) {
        slot->discovery_short_addr = GATEWAY_INVALID_SHORT_ADDR;
    }
}

void gateway_device_reset_discovery(device_slot_t *slot)
{
    if (slot != NULL) {
        slot->discovery_short_addr = GATEWAY_INVALID_SHORT_ADDR;
    }
}

static bool update_bounded_text(
    char target[GATEWAY_BASIC_TEXT_MAX_BYTES], const char *value)
{
    if (target == NULL || value == NULL) {
        return false;
    }
    char bounded[GATEWAY_BASIC_TEXT_MAX_BYTES] = {0};
    strncpy(bounded, value, sizeof(bounded) - 1U);
    if (memcmp(target, bounded, sizeof(bounded)) == 0) {
        return false;
    }
    memcpy(target, bounded, sizeof(bounded));
    return true;
}

bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model)
{
    if (state == NULL) {
        return false;
    }
    bool changed = false;
    if (manufacturer != NULL) {
        changed = update_bounded_text(state->manufacturer, manufacturer) || changed;
    }
    if (model != NULL) {
        changed = update_bounded_text(state->model, model) || changed;
    }
    return changed;
}

binding_state_t *gateway_device_binding_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id, bool create)
{
    if (slot == NULL) {
        return NULL;
    }
    binding_state_t *free_state = NULL;
    for (size_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *state = &slot->bindings[i];
        if (state->in_use && state->endpoint == endpoint &&
            state->cluster_id == cluster_id) {
            return state;
        }
        if (!state->in_use && free_state == NULL) {
            free_state = state;
        }
    }
    if (!create || free_state == NULL) {
        return NULL;
    }
    *free_state = (binding_state_t){
        .in_use = true,
        .endpoint = endpoint,
        .cluster_id = cluster_id,
        .last_status = GATEWAY_CONFIG_STATUS_UNKNOWN,
    };
    return free_state;
}

reporting_state_t *gateway_device_reporting_state(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster_id,
    uint16_t attribute_id, bool create)
{
    if (slot == NULL) {
        return NULL;
    }
    reporting_state_t *free_state = NULL;
    for (size_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *state = &slot->reporting[i];
        if (state->in_use && state->endpoint == endpoint &&
            state->cluster_id == cluster_id && state->attribute_id == attribute_id) {
            return state;
        }
        if (!state->in_use && free_state == NULL) {
            free_state = state;
        }
    }
    if (!create || free_state == NULL) {
        return NULL;
    }
    *free_state = (reporting_state_t){
        .in_use = true,
        .endpoint = endpoint,
        .cluster_id = cluster_id,
        .attribute_id = attribute_id,
        .last_status = GATEWAY_CONFIG_STATUS_UNKNOWN,
    };
    return free_state;
}

endpoint_state_t *gateway_device_endpoint_state(
    device_slot_t *slot, uint8_t endpoint, bool create)
{
    if (slot == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        if (slot->endpoints[i].in_use &&
            slot->endpoints[i].endpoint == endpoint) {
            return &slot->endpoints[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (size_t i = 0; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        if (!slot->endpoints[i].in_use) {
            slot->endpoints[i].in_use = true;
            slot->endpoints[i].endpoint = endpoint;
            return &slot->endpoints[i];
        }
    }
    return NULL;
}
