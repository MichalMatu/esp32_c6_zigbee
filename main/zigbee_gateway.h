#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "gateway_inputs.h"

typedef enum {
    ZIGBEE_GATEWAY_POLICY_QUEUED = 0,
    ZIGBEE_GATEWAY_POLICY_UNSUPPORTED,
    ZIGBEE_GATEWAY_POLICY_ERROR,
} zigbee_gateway_policy_submit_result_t;

esp_err_t zigbee_gateway_start(void);
esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds);
zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change);
