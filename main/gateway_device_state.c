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
