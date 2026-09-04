#include "zigbee_gateway_internal.h"

#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"

#include <ezbee/af.h>
#include <ezbee/zcl/cluster/level.h>
#include <ezbee/zcl/cluster/on_off.h>

#include "gateway_zigbee_input.h"

#define GATEWAY_MAX_COMMAND_CONTEXTS 16U

typedef struct {
    bool in_use;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint32_t request_id;
    gateway_input_id_t input;
} command_context_t;

static command_context_t s_command_contexts[GATEWAY_MAX_COMMAND_CONTEXTS];

static void publish_command_result(
    const gateway_input_id_t *input,
    uint32_t request_id,
    gateway_event_command_result_t result,
    uint8_t status,
    uint8_t tsn)
{
    if (input == NULL || request_id == 0U) {
        return;
    }
    gateway_event_t event = gateway_event_make_input(GATEWAY_EVENT_COMMAND_RESULT, input);
    event.data.command.request_id = request_id;
    event.data.command.result = result;
    event.data.command.status = status;
    event.data.command.tsn = tsn;
    gateway_event_publish(&event);
}

static command_context_t *command_context_alloc(
    device_slot_t *slot,
    const gateway_input_id_t *input,
    uint8_t endpoint,
    uint32_t request_id)
{
    if (slot == NULL || input == NULL || request_id == 0U) {
        return NULL;
    }
    for (size_t i = 0U; i < GATEWAY_MAX_COMMAND_CONTEXTS; ++i) {
        if (!s_command_contexts[i].in_use) {
            s_command_contexts[i] = (command_context_t){
                .in_use = true,
                .device = gateway_device_ref_for(slot),
                .route_short_addr = slot->device.short_addr,
                .endpoint = endpoint,
                .request_id = request_id,
                .input = *input,
            };
            ++slot->pending_requests;
            return &s_command_contexts[i];
        }
    }
    return NULL;
}

static void command_context_release(command_context_t *context)
{
    if (context == NULL || !context->in_use) {
        return;
    }
    device_slot_t *slot = gateway_device_from_ref(context->device, true);
    if (slot != NULL && slot->pending_requests != 0U) {
        --slot->pending_requests;
        gateway_device_maybe_reclaim(slot);
    }
    context->in_use = false;
}

static void command_confirm_callback(ezb_af_user_cnf_t *cnf, void *user_ctx)
{
    command_context_t *context = user_ctx;
    if (context == NULL || !context->in_use) {
        return;
    }
    const uint8_t status = cnf == NULL ? 0xffU : cnf->status;
    const uint8_t tsn = cnf == NULL ? 0xffU : cnf->tsn;
    device_slot_t *slot = gateway_device_from_ref(context->device, true);
    const bool route_current = gateway_device_route_is_current(
        slot, context->route_short_addr);
    publish_command_result(
        &context->input,
        context->request_id,
        cnf != NULL && status == 0U && route_current ?
            GATEWAY_EVENT_COMMAND_TRANSMITTED : GATEWAY_EVENT_COMMAND_ERROR,
        status,
        tsn);
    command_context_release(context);
}

void zigbee_gateway_execute_command(
    uint32_t request_id,
    const gateway_input_id_t *input,
    const gateway_command_plan_t *plan)
{
    if (request_id == 0U || input == NULL || plan == NULL) {
        return;
    }
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (!gateway_zigbee_parse_input_identity(input, ieee, &endpoint)) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_INVALID, 0xffU, 0xffU);
        return;
    }
    device_slot_t *slot = gateway_device_find_by_ieee(ieee, false);
    endpoint_state_t *state = slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, endpoint, false);
    if (slot == NULL || state == NULL || slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        return;
    }
    if ((state->input_profile.commandable & plan->capability) == 0U) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_UNSUPPORTED, 0xffU, 0xffU);
        return;
    }
    command_context_t *context = command_context_alloc(
        slot, input, endpoint, request_id);
    if (context == NULL) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        return;
    }
    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(ZIGBEE_GATEWAY_LOCK_TIMEOUT_MS))) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        command_context_release(context);
        return;
    }
    ezb_err_t send_result = (ezb_err_t)1;
    if (plan->kind == GATEWAY_COMMAND_SET_ON_OFF) {
        const ezb_zcl_on_off_cmd_t request = {
            .cmd_ctrl = {
                .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
                .dst_ep = endpoint,
                .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
                .dis_default_rsp = false,
                .cnf_ctx = {
                    .cb = command_confirm_callback,
                    .user_ctx = context,
                },
            },
        };
        send_result = plan->target_on ?
            ezb_zcl_on_off_on_cmd_req(&request) : ezb_zcl_on_off_off_cmd_req(&request);
    } else if (plan->kind == GATEWAY_COMMAND_SET_LEVEL) {
        const ezb_zcl_level_move_to_level_cmd_t request = {
            .cmd_ctrl = {
                .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
                .dst_ep = endpoint,
                .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
                .dis_default_rsp = false,
                .cnf_ctx = {
                    .cb = command_confirm_callback,
                    .user_ctx = context,
                },
            },
            .payload = {
                .level = plan->level,
                .transition_time = plan->transition_time,
                .options_mask = 0U,
                .options_override = 0U,
            },
        };
        send_result = ezb_zcl_level_move_to_level_cmd_req(&request);
    }
    esp_zigbee_lock_release();
    if (send_result != EZB_ERR_NONE && context->in_use) {
        publish_command_result(
            input, request_id,
            GATEWAY_EVENT_COMMAND_ERROR, 0xffU, 0xffU);
        command_context_release(context);
    }
}
