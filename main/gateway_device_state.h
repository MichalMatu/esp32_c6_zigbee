#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_events.h"

#define GATEWAY_MAX_DEVICES 16U
#define GATEWAY_MAX_ENDPOINTS_PER_DEVICE 8U
#define GATEWAY_INVALID_SHORT_ADDR 0xffffU
#define GATEWAY_BASIC_TEXT_MAX_BYTES 24U

typedef enum { SLOT_EMPTY, SLOT_ACTIVE, SLOT_REJOIN_PENDING, SLOT_LEAVING } slot_state_t;
typedef enum { BASIC_NOT_SCHEDULED, BASIC_SCHEDULED, BASIC_COMPLETE } basic_state_t;

typedef struct {
    bool in_use;
    uint8_t endpoint;
    basic_state_t basic_state;
    uint32_t basic_scheduled_at_ms;
    uint8_t reporting_requested;
    uint8_t reporting_configured;
    uint32_t reporting_requested_at_ms;
    uint8_t binding_requested;
    uint8_t binding_configured;
    uint32_t binding_requested_at_ms;
    gateway_input_capabilities_t input_capabilities;
    char manufacturer[GATEWAY_BASIC_TEXT_MAX_BYTES];
    char model[GATEWAY_BASIC_TEXT_MAX_BYTES];
    bool input_announced;
} endpoint_state_t;

typedef struct {
    slot_state_t state;
    uint32_t generation;
    gateway_device_id_t device;
    uint16_t previous_short_addr;
    uint16_t discovery_short_addr;
    uint8_t pending_jobs;
    uint8_t pending_requests;
    endpoint_state_t endpoints[GATEWAY_MAX_ENDPOINTS_PER_DEVICE];
} device_slot_t;

typedef struct {
    uint8_t index;
    uint32_t generation;
} device_ref_t;

device_slot_t *gateway_device_find_by_ieee(
    const uint8_t ieee[8], bool include_non_active);
device_slot_t *gateway_device_find_by_short(
    uint16_t short_addr, bool include_non_active);
device_ref_t gateway_device_ref_for(const device_slot_t *slot);
device_slot_t *gateway_device_from_ref(
    device_ref_t ref, bool include_non_active);
void gateway_device_maybe_reclaim(device_slot_t *slot);
device_slot_t *gateway_device_upsert(
    uint16_t short_addr, const uint8_t ieee[8]);
bool gateway_device_claim_discovery(device_slot_t *slot);
void gateway_device_release_discovery(device_slot_t *slot, uint16_t short_addr);
void gateway_device_reset_discovery(device_slot_t *slot);
bool gateway_device_endpoint_update_basic(
    endpoint_state_t *state, const char *manufacturer, const char *model);
endpoint_state_t *gateway_device_endpoint_state(
    device_slot_t *slot, uint8_t endpoint, bool create);
