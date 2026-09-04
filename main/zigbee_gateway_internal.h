#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gateway_command_policy.h"
#include "gateway_device_state.h"
#include "gateway_events.h"
#include "gateway_reporting_policy.h"

#define ZIGBEE_GATEWAY_ENDPOINT 1U
#define ZIGBEE_GATEWAY_PROFILE_ID 0x0104U
#define ZIGBEE_GATEWAY_DEVICE_ID 0x0000U
#define ZIGBEE_GATEWAY_CHANNEL_MASK 0x07fff800UL
#define ZIGBEE_GATEWAY_LOCK_TIMEOUT_MS 100U

#define ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE 0x0500U

bool zigbee_gateway_work_init(void);
bool zigbee_gateway_work_start(void);
bool zigbee_gateway_schedule_active_discovery(device_slot_t *slot);
bool zigbee_gateway_schedule_ias_cie(device_slot_t *slot, uint8_t endpoint);
bool zigbee_gateway_enqueue_reporting_request(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    const gateway_reporting_plan_t *plan);
bool zigbee_gateway_enqueue_command_request(
    uint32_t request_id,
    const gateway_input_id_t *input,
    const gateway_command_plan_t *plan);
void zigbee_gateway_execute_command(
    uint32_t request_id,
    const gateway_input_id_t *input,
    const gateway_command_plan_t *plan);

endpoint_state_t *zigbee_gateway_endpoint_state(
    device_slot_t *slot, uint8_t endpoint, bool create);
bool zigbee_gateway_publish_input(
    device_slot_t *slot, endpoint_state_t *state, bool available);
void zigbee_gateway_publish_reporting_result(
    const gateway_device_id_t *device,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint8_t zcl_status,
    uint32_t request_id,
    gateway_event_config_result_t result);
void zigbee_gateway_register_zcl_handlers(void);
