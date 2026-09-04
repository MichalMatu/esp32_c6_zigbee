#include "zigbee_gateway_internal.h"

#include <string.h>

#include "esp_zigbee.h"

#include <ezbee/af.h>
#include <ezbee/zcl/cluster/ias_zone.h>
#include <ezbee/zcl/cluster/poll_control.h>
#include <ezbee/zcl/zcl_core.h>
#include <ezbee/zcl/zcl_general_cmd.h>
#include <ezbee/zdo/zdo_dev_srv_disc.h>

#include "gateway_zcl_value.h"
#include "gateway_zigbee_input.h"

#define ZIGBEE_GATEWAY_REQUEST_STALE_MS 10000U
#define ZIGBEE_GATEWAY_FAST_POLL_TIMEOUT_QUARTER_SECONDS 20U

#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U
#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U
#define ZCL_ATTR_IAS_ZONE_TYPE 0x0001U
#define ZCL_ATTR_IAS_ZONE_STATUS 0x0002U
#define ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH 0x0015U

static gateway_device_id_t device_from_header(const ezb_zcl_cmd_hdr_t *header)
{
    gateway_device_id_t device = {.short_addr = GATEWAY_INVALID_SHORT_ADDR};
    if (header == NULL) {
        return device;
    }
    if (header->src_addr.addr_mode == EZB_ADDR_MODE_SHORT) {
        device.short_addr = header->src_addr.u.short_addr;
        device_slot_t *slot = gateway_device_find_by_short(device.short_addr, false);
        if (slot != NULL) {
            device = slot->device;
        }
    } else if (header->src_addr.addr_mode == EZB_ADDR_MODE_EXT) {
        memcpy(
            device.ieee,
            header->src_addr.u.extended_addr.u8,
            sizeof(device.ieee)
        );
        device.ieee_valid = true;
        device_slot_t *slot = gateway_device_find_by_ieee(
            header->src_addr.u.extended_addr.u8, false
        );
        if (slot != NULL) {
            device = slot->device;
        }
    }
    return device;
}

static device_slot_t *recover_report_source(const ezb_zcl_cmd_hdr_t *header)
{
    if (header == NULL || header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT) {
        return NULL;
    }
    const ezb_shortaddr_t short_addr = header->src_addr.u.short_addr;
    device_slot_t *slot = gateway_device_find_by_short(short_addr, false);
    if (slot != NULL) {
        return slot;
    }

    ezb_extaddr_t ieee;
    slot = ezb_address_extended_by_short(short_addr, &ieee) == EZB_ERR_NONE ?
        gateway_device_upsert(short_addr, ieee.u8) : gateway_device_upsert(short_addr, NULL);
    if (slot != NULL && slot->discovery_short_addr != slot->device.short_addr &&
        !zigbee_gateway_schedule_active_discovery(slot)) {
        gateway_event_warning(&slot->device, "report recovery discovery queue full");
    }
    return slot;
}

bool zigbee_gateway_publish_input(
    device_slot_t *slot, endpoint_state_t *state, bool available)
{
    if (slot == NULL || state == NULL || state->input_profile.readable == 0U) {
        return false;
    }
    gateway_input_id_t input;
    if (!gateway_zigbee_stable_input_id(
            slot->device.ieee, slot->device.ieee_valid,
            state->endpoint, &input)) {
        return false;
    }
    gateway_event_t event = gateway_event_make_input(
        available ? GATEWAY_EVENT_INPUT_AVAILABLE : GATEWAY_EVENT_INPUT_UNAVAILABLE,
        &input);
    event.endpoint = state->endpoint;
    event.data.input_desc.profile = state->input_profile;
    strncpy(
        event.data.input_desc.manufacturer, state->manufacturer,
        sizeof(event.data.input_desc.manufacturer) - 1U);
    strncpy(
        event.data.input_desc.model, state->model,
        sizeof(event.data.input_desc.model) - 1U);
    if (!gateway_event_publish(&event)) {
        return false;
    }
    state->input_announced = available;
    return true;
}

static bool publish_ias_contact_measurement(
    device_slot_t *slot, endpoint_state_t *state, uint16_t zone_status)
{
    if (slot == NULL || state == NULL || !state->ias_zone_type_known) {
        return false;
    }
    gateway_measurement_kind_t kind;
    gateway_unit_t unit;
    double value;
    if (!gateway_zcl_normalize_ias_contact(
            state->ias_zone_type, zone_status, &kind, &unit, &value)) {
        return false;
    }
    gateway_input_id_t input = {0};
    if (!gateway_zigbee_stable_input_id(
            slot->device.ieee, slot->device.ieee_valid,
            state->endpoint, &input)) {
        return false;
    }
    gateway_event_t event = gateway_event_make_input(
        GATEWAY_EVENT_MEASUREMENT, &input);
    event.endpoint = state->endpoint;
    event.data.measurement = (gateway_measurement_t){
        .kind = kind,
        .unit = unit,
        .value = value,
    };
    return gateway_event_publish(&event);
}

static void apply_ias_zone_type(
    device_slot_t *slot, endpoint_state_t *state, uint16_t zone_type)
{
    if (slot == NULL || state == NULL) {
        return;
    }
    const gateway_input_capabilities_t old_readable = state->input_profile.readable;
    gateway_input_capabilities_t new_readable =
        old_readable & ~GATEWAY_INPUT_CAP_CONTACT_OPEN;
    if (zone_type == ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH) {
        new_readable |= GATEWAY_INPUT_CAP_CONTACT_OPEN;
    }

    if (state->input_announced && old_readable != 0U && new_readable == 0U) {
        (void)zigbee_gateway_publish_input(slot, state, false);
    }
    state->ias_zone_type = zone_type;
    state->ias_zone_type_known = true;
    state->ias_zone_type_read_requested = false;
    state->input_profile.readable = new_readable;

    if (new_readable != 0U &&
        (!state->input_announced || new_readable != old_readable)) {
        (void)zigbee_gateway_publish_input(slot, state, true);
    }
    if (!zigbee_gateway_schedule_ias_cie(slot, state->endpoint)) {
        gateway_event_warning(&slot->device, "IAS CIE write queue full");
    }
    if (state->ias_zone_status_valid) {
        (void)publish_ias_contact_measurement(
            slot, state, state->ias_zone_status);
    }
}

static void publish_ias_zone_type_read(
    const ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT ||
        header->cluster_id != ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE) {
        return;
    }
    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = device.ieee_valid ?
        gateway_device_find_by_ieee(device.ieee, false) :
        gateway_device_find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, header->src_ep, false);
    if (state == NULL) {
        return;
    }

    bool seen = false;
    for (ezb_zcl_read_attr_rsp_variable_t *item = message->in.variables;
         item != NULL; item = item->next) {
        if (item->attr_id != ZCL_ATTR_IAS_ZONE_TYPE) {
            continue;
        }
        seen = true;
        if (item->status == EZB_ZCL_STATUS_SUCCESS &&
            item->attr_type == EZB_ZCL_ATTR_TYPE_ENUM16 &&
            item->attr_value != NULL) {
            uint16_t zone_type = 0U;
            memcpy(&zone_type, item->attr_value, sizeof(zone_type));
            apply_ias_zone_type(slot, state, zone_type);
            return;
        }
    }
    if (seen) {
        state->ias_zone_type_read_requested = false;
        gateway_event_warning(&slot->device, "IAS ZoneType read failed");
    }
}

static void handle_ias_zone_enroll(
    ezb_zcl_ias_zone_enroll_req_message_t *message)
{
    if (message == NULL) {
        return;
    }
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT ||
        header->cluster_id != ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE) {
        return;
    }
    device_slot_t *slot = recover_report_source(header);
    endpoint_state_t *state =
        slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, header->src_ep, false);
    if (slot == NULL || state == NULL) {
        return;
    }
    const device_ref_t ref = gateway_device_ref_for(slot);
    const size_t endpoint_index = (size_t)(state - slot->endpoints);
    const uint8_t zone_id = (uint8_t)(
        (size_t)ref.index * GATEWAY_MAX_ENDPOINTS_PER_DEVICE + endpoint_index);
    const ezb_zcl_ias_zone_enroll_rsp_cmd_t response = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = header->src_ep,
            .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
            .dis_default_rsp = false,
        },
        .payload = {
            .enroll_rsp_code = EZB_ZCL_IAS_ZONE_ENROLL_RESPONSE_CODE_SUCCESS,
            .zone_id = zone_id,
        },
    };
    if (ezb_zcl_ias_zone_enroll_cmd_resp(&response) != EZB_ERR_NONE) {
        gateway_event_warning(&slot->device, "IAS enroll response failed");
    }
}

static void handle_ias_zone_status_change(
    ezb_zcl_ias_zone_status_change_notif_message_t *message)
{
    if (message == NULL) {
        return;
    }
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT) {
        return;
    }
    device_slot_t *slot = recover_report_source(header);
    endpoint_state_t *state =
        slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, header->src_ep, false);
    if (state == NULL) {
        return;
    }
    state->ias_zone_status = message->in.payload.zone_status;
    state->ias_zone_status_valid = true;
    (void)publish_ias_contact_measurement(
        slot, state, message->in.payload.zone_status);
}

static void publish_report(const ezb_zcl_cmd_report_attr_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT) {
        return;
    }
    device_slot_t *slot = recover_report_source(header);
    gateway_device_id_t device =
        slot == NULL ? device_from_header(header) : slot->device;

    gateway_input_id_t input = {0};
    const bool stable_input = gateway_zigbee_stable_input_id(
        device.ieee, device.ieee_valid, header->src_ep, &input);

    for (ezb_zcl_report_attr_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        gateway_measurement_kind_t kind;
        gateway_unit_t unit;
        double value;
        bool handled = false;
        if (header->cluster_id == ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE &&
            item->attr_id == ZCL_ATTR_IAS_ZONE_STATUS &&
            item->attr_type == EZB_ZCL_ATTR_TYPE_UINT16 &&
            item->attr_value != NULL && slot != NULL) {
            endpoint_state_t *state = zigbee_gateway_endpoint_state(
                slot, header->src_ep, false);
            if (state != NULL) {
                uint16_t zone_status = 0U;
                memcpy(&zone_status, item->attr_value, sizeof(zone_status));
                state->ias_zone_status = zone_status;
                state->ias_zone_status_valid = true;
                handled = publish_ias_contact_measurement(
                    slot, state, zone_status);
            }
        }
        if (!handled && stable_input && gateway_zcl_normalize(
                header->cluster_id,
                item->attr_id,
                item->attr_type,
                item->attr_value,
                &kind,
                &unit,
                &value)) {
            gateway_event_t event = gateway_event_make_input(
                GATEWAY_EVENT_MEASUREMENT, &input);
            event.endpoint = header->src_ep;
            event.data.measurement = (gateway_measurement_t){
                .kind = kind,
                .unit = unit,
                .value = value,
            };
            gateway_event_publish(&event);
            handled = true;
        }
        if (!handled) {
            gateway_event_t event = gateway_event_make(
                GATEWAY_EVENT_RAW_ATTRIBUTE, &device
            );
            event.endpoint = header->src_ep;
            event.data.raw.cluster_id = header->cluster_id;
            event.data.raw.attribute_id = item->attr_id;
            event.data.raw.zcl_type = item->attr_type;
            event.data.raw.original_length = gateway_zcl_attr_size(
                item->attr_type, item->attr_value
            );
            event.data.raw.copied_length =
                event.data.raw.original_length > GATEWAY_RAW_ATTRIBUTE_MAX_BYTES ?
                GATEWAY_RAW_ATTRIBUTE_MAX_BYTES : event.data.raw.original_length;
            event.data.raw.truncated =
                event.data.raw.original_length > GATEWAY_RAW_ATTRIBUTE_MAX_BYTES;
            if (event.data.raw.copied_length != 0U) {
                memcpy(
                    event.data.raw.bytes,
                    item->attr_value,
                    event.data.raw.copied_length
                );
            }
            gateway_event_publish(&event);
        }
    }
}

static void publish_basic_read(const ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT ||
        header->cluster_id != EZB_ZCL_CLUSTER_ID_BASIC) {
        return;
    }

    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = gateway_device_find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, header->src_ep, false);
    bool seen = false;
    bool model_changed = false;
    for (ezb_zcl_read_attr_rsp_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        if (item->attr_id != ZCL_ATTR_BASIC_MANUFACTURER_NAME &&
            item->attr_id != ZCL_ATTR_BASIC_MODEL_IDENTIFIER) {
            continue;
        }
        seen = true;
        if (item->status != EZB_ZCL_STATUS_SUCCESS ||
            item->attr_type != EZB_ZCL_ATTR_TYPE_STRING ||
            item->attr_value == NULL) {
            continue;
        }
        const uint8_t *string = item->attr_value;
        if (string[0] == 0xffU) {
            continue;
        }
        gateway_event_t event = gateway_event_make(GATEWAY_EVENT_BASIC, &device);
        event.endpoint = header->src_ep;
        strncpy(
            event.data.text.key,
            item->attr_id == ZCL_ATTR_BASIC_MANUFACTURER_NAME ?
                "manufacturer" : "model",
            sizeof(event.data.text.key) - 1U
        );
        const size_t len = string[0] < GATEWAY_TEXT_MAX_BYTES - 1U ?
            string[0] : GATEWAY_TEXT_MAX_BYTES - 1U;
        memcpy(event.data.text.value, string + 1, len);
        event.data.text.value[len] = '\0';
        if (state != NULL) {
            const bool changed = item->attr_id == ZCL_ATTR_BASIC_MANUFACTURER_NAME ?
                gateway_device_endpoint_update_basic(
                    state, event.data.text.value, NULL) :
                gateway_device_endpoint_update_basic(
                    state, NULL, event.data.text.value);
            if (item->attr_id == ZCL_ATTR_BASIC_MODEL_IDENTIFIER && changed) {
                model_changed = true;
            }
        }
        gateway_event_publish(&event);
    }

    if (seen && state != NULL) {
        state->basic_state = BASIC_COMPLETE;
    }
    if (model_changed && state != NULL && state->input_announced) {
        (void)zigbee_gateway_publish_input(slot, state, true);
    }
}

static void publish_config_response(
    const ezb_zcl_cmd_config_report_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT) {
        return;
    }
    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = gateway_device_find_by_short(device.short_addr, false);

    for (ezb_zcl_config_report_rsp_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        reporting_state_t *reporting = slot == NULL ? NULL :
            gateway_device_reporting_state(
                slot, header->src_ep, header->cluster_id, item->attr_id, false);
        uint32_t request_id = 0U;
        bool request_clamped = false;
        if (reporting != NULL) {
            request_id = reporting->request_id;
            request_clamped = reporting->request_clamped;
            reporting->requested = false;
            reporting->last_status = item->status;
            if (item->status == EZB_ZCL_STATUS_SUCCESS) {
                reporting->configured = true;
            }
            reporting->request_id = 0U;
            reporting->request_clamped = false;
        }
        const gateway_event_config_result_t result =
            item->status == EZB_ZCL_STATUS_SUCCESS ?
                (request_clamped ? GATEWAY_EVENT_CONFIG_CLAMPED :
                                   GATEWAY_EVENT_CONFIG_APPLIED) :
                GATEWAY_EVENT_CONFIG_ERROR;
        zigbee_gateway_publish_reporting_result(
            &device, header->src_ep, header->cluster_id, item->attr_id,
            item->status, request_id, result);
    }
}

static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)
{
    if (message == NULL) {
        return;
    }
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    message->out.start_fast_poll = true;
    message->out.fast_poll_timeout = ZIGBEE_GATEWAY_FAST_POLL_TIMEOUT_QUARTER_SECONDS;

    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != ZIGBEE_GATEWAY_ENDPOINT ||
        header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT) {
        return;
    }
    device_slot_t *slot = gateway_device_upsert(header->src_addr.u.short_addr, NULL);
    if (slot == NULL) {
        return;
    }

    const uint32_t now_ms = gateway_uptime_ms();
    for (size_t i = 0; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        if (!slot->endpoints[i].in_use) {
            continue;
        }
        endpoint_state_t *state = &slot->endpoints[i];
        if (state->basic_state == BASIC_SCHEDULED &&
            (uint32_t)(now_ms - state->basic_scheduled_at_ms) >=
                ZIGBEE_GATEWAY_REQUEST_STALE_MS) {
            state->basic_state = BASIC_NOT_SCHEDULED;
        }
        if (state->ias_zone_type_read_requested &&
            (uint32_t)(now_ms - state->ias_zone_type_requested_at_ms) >=
                ZIGBEE_GATEWAY_REQUEST_STALE_MS) {
            state->ias_zone_type_read_requested = false;
        }
    }
    for (size_t i = 0; i < GATEWAY_MAX_BINDING_STATES_PER_DEVICE; ++i) {
        binding_state_t *binding = &slot->bindings[i];
        if (binding->in_use && binding->requested &&
            (uint32_t)(now_ms - binding->requested_at_ms) >=
                ZIGBEE_GATEWAY_REQUEST_STALE_MS) {
            binding->requested = false;
        }
    }
    for (size_t i = 0; i < GATEWAY_MAX_REPORTING_STATES_PER_DEVICE; ++i) {
        reporting_state_t *reporting = &slot->reporting[i];
        if (reporting->in_use && reporting->requested &&
            (uint32_t)(now_ms - reporting->requested_at_ms) >=
                ZIGBEE_GATEWAY_REQUEST_STALE_MS) {
            if (reporting->request_id != 0U) {
                zigbee_gateway_publish_reporting_result(
                    &slot->device, reporting->endpoint, reporting->cluster_id,
                    reporting->attribute_id, GATEWAY_CONFIG_STATUS_UNKNOWN,
                    reporting->request_id, GATEWAY_EVENT_CONFIG_ERROR);
            }
            reporting->requested = false;
            reporting->request_id = 0U;
            reporting->request_clamped = false;
        }
    }

    gateway_event_t event = gateway_event_make(
        GATEWAY_EVENT_DEVICE_CHECK_IN, &slot->device
    );
    event.endpoint = header->src_ep;
    gateway_event_publish(&event);
    if (slot->discovery_short_addr != slot->device.short_addr &&
        !zigbee_gateway_schedule_active_discovery(slot)) {
        gateway_event_warning(&slot->device, "check-in discovery queue full");
    }
}

static void zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id == EZB_ZCL_CORE_REPORT_ATTR_CB_ID) {
        publish_report(message);
    } else if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {
        publish_basic_read(message);
        publish_ias_zone_type_read(message);
    } else if (callback_id == EZB_ZCL_CORE_IAS_ZONE_ENROLL_CB_ID) {
        handle_ias_zone_enroll(message);
    } else if (callback_id == EZB_ZCL_CORE_IAS_ZONE_STATUS_CHANGE_NOTIF_CB_ID) {
        handle_ias_zone_status_change(message);
    } else if (callback_id == EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID) {
        publish_config_response(message);
    } else if (callback_id == EZB_ZCL_CORE_POLL_CONTROL_CHECK_IN_CB_ID) {
        handle_check_in(message);
    }
}


void zigbee_gateway_register_zcl_handlers(void)
{
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
}
