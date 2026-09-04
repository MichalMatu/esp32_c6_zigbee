#include "zigbee_gateway.h"

#include <string.h>

#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ezbee/af.h>
#include <ezbee/app_signals.h>
#include <ezbee/bdb.h>
#include <ezbee/secur.h>
#include <ezbee/zcl/cluster/ias_zone.h>
#include <ezbee/zcl/cluster/ias_zone_desc.h>

#include "gateway_command_policy.h"
#include "gateway_device_state.h"
#include "gateway_events.h"
#include "gateway_reporting_policy.h"
#include "gateway_zigbee_input.h"
#include "zigbee_gateway_internal.h"

static portMUX_TYPE s_gateway_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_stack_ready;

static void set_stack_ready(bool ready)
{
    portENTER_CRITICAL(&s_gateway_state_lock);
    s_stack_ready = ready;
    portEXIT_CRITICAL(&s_gateway_state_lock);
}

static bool stack_is_ready(void)
{
    portENTER_CRITICAL(&s_gateway_state_lock);
    const bool ready = s_stack_ready;
    portEXIT_CRITICAL(&s_gateway_state_lock);
    return ready;
}

static void network_event(gateway_event_kind_t kind)
{
    gateway_event_t event = gateway_event_make(kind, NULL);
    gateway_event_publish(&event);
}

static void announce_and_discover(
    gateway_event_kind_t kind,
    ezb_shortaddr_t short_addr,
    const ezb_extaddr_t *ieee)
{
    device_slot_t *existing =
        ieee == NULL ? NULL : gateway_device_find_by_ieee(ieee->u8, true);
    const ezb_shortaddr_t old_short_addr = existing == NULL ?
        GATEWAY_INVALID_SHORT_ADDR :
        (existing->device.short_addr == GATEWAY_INVALID_SHORT_ADDR ?
            existing->previous_short_addr : existing->device.short_addr);

    device_slot_t *slot = gateway_device_upsert(
        short_addr, ieee == NULL ? NULL : ieee->u8);
    if (slot == NULL) {
        gateway_event_warning(NULL, "device registry capacity exhausted");
        return;
    }

    gateway_event_t event = gateway_event_make(kind, &slot->device);
    if (kind == GATEWAY_EVENT_DEVICE_REJOIN) {
        event.data.rejoin.old_short_addr = old_short_addr;
        event.data.rejoin.new_short_addr = slot->device.short_addr;
    }
    gateway_event_publish(&event);
    if (kind == GATEWAY_EVENT_DEVICE_REJOIN) {
        for (size_t i = 0U; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
            endpoint_state_t *state = &slot->endpoints[i];
            if (state->in_use && state->ias_zone_type_known &&
                !zigbee_gateway_schedule_ias_cie(slot, state->endpoint)) {
                gateway_event_warning(&slot->device, "IAS CIE rejoin refresh queue full");
            }
        }
    }
    if (slot->discovery_short_addr != slot->device.short_addr &&
        !zigbee_gateway_schedule_active_discovery(slot)) {
        gateway_event_warning(&slot->device, "active endpoint queue full");
    }
}

static void publish_device_update(const ezb_zdo_signal_device_update_params_t *p)
{
    if (p == NULL) {
        return;
    }
    device_slot_t *slot = gateway_device_upsert(p->short_addr, p->device_addr.u8);
    if (slot == NULL) {
        gateway_event_warning(NULL, "device registry capacity exhausted");
        return;
    }
    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_DEVICE_UPDATE, &slot->device);
    event.data.device_update.status = p->status;
    event.data.device_update.tc_action = p->tc_action;
    gateway_event_publish(&event);
}

static void publish_device_authorized(const ezb_zdo_signal_device_authorized_params_t *p)
{
    if (p == NULL) {
        return;
    }
    device_slot_t *slot = gateway_device_upsert(p->short_addr, p->device_addr.u8);
    if (slot == NULL) {
        gateway_event_warning(NULL, "device registry capacity exhausted");
        return;
    }
    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_DEVICE_AUTHORIZED, &slot->device);
    event.data.authorization.type = p->type;
    event.data.authorization.status = p->status;
    gateway_event_publish(&event);
}

static gateway_device_id_t leave_device_id(
    const device_slot_t *slot,
    ezb_shortaddr_t short_addr,
    const ezb_extaddr_t *ieee)
{
    gateway_device_id_t device = {.short_addr = short_addr};
    if (slot != NULL) {
        device = slot->device;
    }
    if (device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        device.short_addr = short_addr;
    }
    if (ieee != NULL) {
        memcpy(device.ieee, ieee->u8, sizeof(device.ieee));
        device.ieee_valid = true;
    }
    return device;
}

static void publish_slot_inputs_unavailable(device_slot_t *slot)
{
    if (slot == NULL || !slot->device.ieee_valid) {
        return;
    }
    for (size_t i = 0U; i < GATEWAY_MAX_ENDPOINTS_PER_DEVICE; ++i) {
        endpoint_state_t *state = &slot->endpoints[i];
        if (state->in_use && state->input_announced) {
            (void)zigbee_gateway_publish_input(slot, state, false);
        }
    }
}

static void device_left(
    ezb_shortaddr_t short_addr,
    const ezb_extaddr_t *ieee,
    bool leave_type_known,
    ezb_zdo_leave_type_t leave_type)
{
    device_slot_t *slot = ieee == NULL ? NULL : gateway_device_find_by_ieee(ieee->u8, true);
    if (slot == NULL) {
        slot = gateway_device_find_by_short(short_addr, true);
    }
    const gateway_device_id_t device = leave_device_id(slot, short_addr, ieee);

    gateway_event_kind_t kind = GATEWAY_EVENT_DEVICE_LEAVE_UNKNOWN;
    bool retained = true;
    if (leave_type_known && leave_type == EZB_ZDO_LEAVE_TYPE_RESET) {
        kind = GATEWAY_EVENT_DEVICE_LEAVE_RESET;
        retained = false;
    } else if (leave_type_known && leave_type == EZB_ZDO_LEAVE_TYPE_REJOIN) {
        kind = GATEWAY_EVENT_DEVICE_LEAVE_REJOIN;
    }

    gateway_event_t event = gateway_event_make(kind, &device);
    event.data.leave.leave_type = leave_type_known ? leave_type : UINT8_MAX;
    event.data.leave.record_retained = retained;
    gateway_event_publish(&event);

    if (slot == NULL) {
        return;
    }
    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET ||
        kind == GATEWAY_EVENT_DEVICE_LEAVE_REJOIN) {
        publish_slot_inputs_unavailable(slot);
        gateway_device_reset_discovery(slot);
    }
    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET) {
        slot->state = SLOT_LEAVING;
        gateway_device_maybe_reclaim(slot);
        return;
    }
    if (slot->device.short_addr != GATEWAY_INVALID_SHORT_ADDR) {
        slot->previous_short_addr = slot->device.short_addr;
    }
    slot->device.short_addr = GATEWAY_INVALID_SHORT_ADDR;
    slot->state = SLOT_REJOIN_PENDING;
}

static bool app_signal_handler(const ezb_app_signal_t *signal)
{
    const ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    const void *params = ezb_app_signal_get_params(signal);

    if (type == EZB_ZDO_SIGNAL_SKIP_STARTUP) {
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
    } else if (type == EZB_BDB_SIGNAL_DEVICE_FIRST_START) {
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
    } else if (type == EZB_BDB_SIGNAL_DEVICE_REBOOT) {
        network_event(GATEWAY_EVENT_NETWORK_RESTORED);
    } else if (type == EZB_BDB_SIGNAL_FORMATION) {
        const ezb_bdb_signal_simple_params_t *formation = params;
        if (formation != NULL && formation->status == EZB_BDB_STATUS_SUCCESS) {
            network_event(GATEWAY_EVENT_NETWORK_FORMED);
            ezb_bdb_open_network(180U);
        }
    } else if (type == EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS) {
        const ezb_nwk_signal_permit_join_status_params_t *permit = params;
        gateway_event_t event = gateway_event_make(GATEWAY_EVENT_PERMIT_JOIN, NULL);
        event.data.permit.duration = permit == NULL ? 0U : permit->duration;
        gateway_event_publish(&event);
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const ezb_zdo_signal_device_annce_params_t *p = params;
        if (p != NULL) {
            announce_and_discover(
                GATEWAY_EVENT_DEVICE_ANNOUNCE, p->short_addr, &p->device_addr
            );
        }
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_UPDATE) {
        const ezb_zdo_signal_device_update_params_t *p = params;
        if (p != NULL && p->status == EZB_ZDO_UPDDEV_DEVICE_LEFT) {
            device_left(p->short_addr, &p->device_addr, false, UINT8_MAX);
        } else if (p != NULL &&
            (p->status == EZB_ZDO_UPDDEV_SECURE_REJOIN ||
             p->status == EZB_ZDO_UPDDEV_TC_REJOIN)) {
            announce_and_discover(
                GATEWAY_EVENT_DEVICE_REJOIN, p->short_addr, &p->device_addr
            );
        } else if (p != NULL) {
            publish_device_update(p);
        }
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_AUTHORIZED) {
        publish_device_authorized(params);
    } else if (type == EZB_ZDO_SIGNAL_LEAVE_INDICATION) {
        const ezb_zdo_signal_leave_indication_params_t *p = params;
        if (p != NULL) {
            device_left(
                p->short_addr, &p->device_addr, true, p->leave_type
            );
        }
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_UNAVAILABLE) {
        const ezb_zdo_signal_device_unavailable_params_t *p = params;
        if (p != NULL) {
            gateway_device_id_t device = {.short_addr = p->short_addr};
            memcpy(device.ieee, p->device_addr.u8, sizeof(device.ieee));
            device.ieee_valid = true;
            device_slot_t *slot = gateway_device_find_by_ieee(p->device_addr.u8, false);
            if (slot == NULL) {
                slot = gateway_device_find_by_short(p->short_addr, false);
            }
            if (slot != NULL) {
                device = slot->device;
            }
            gateway_event_t event = gateway_event_make(
                GATEWAY_EVENT_DEVICE_UNAVAILABLE, &device
            );
            gateway_event_publish(&event);
        }
    }
    return false;
}

static void fail_zigbee_task(const char *message)
{
    gateway_event_warning(NULL, message);
    vTaskDelete(NULL);
}

static void zigbee_task(void *arg)
{
    (void)arg;
    set_stack_ready(false);
    const esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR,
            .install_code_policy = false,
            .zczr_config = {.max_children = 32U},
        },
        .platform_config = {
            .storage_partition_name = "zb_storage",
            .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
        },
    };
    if (esp_zigbee_init(&config) != ESP_OK) {
        fail_zigbee_task("esp_zigbee_init failed");
        return;
    }
    if (!zigbee_gateway_work_init()) {
        fail_zigbee_task("discovery queue creation failed");
        return;
    }

    ezb_bdb_set_primary_channel_set(ZIGBEE_GATEWAY_CHANNEL_MASK);
    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = ZIGBEE_GATEWAY_ENDPOINT,
        .app_profile_id = ZIGBEE_GATEWAY_PROFILE_ID,
        .app_device_id = ZIGBEE_GATEWAY_DEVICE_ID,
        .app_device_version = 1U,
    };
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t endpoint = ezb_af_create_gateway_endpoint(&endpoint_config);
    ezb_zcl_cluster_desc_t ias_zone_client = ezb_zcl_ias_zone_create_cluster_desc(
        NULL, EZB_ZCL_CLUSTER_CLIENT);
    if (device == EZB_INVALID_AF_DEVICE_DESC ||
        endpoint == EZB_INVALID_AF_EP_DESC ||
        ias_zone_client == EZB_INVALID_ZCL_CLUSTER_DESC ||
        ezb_af_endpoint_add_cluster_desc(endpoint, ias_zone_client) != EZB_ERR_NONE ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||
        ezb_af_device_desc_register(device) != EZB_ERR_NONE) {
        fail_zigbee_task("gateway endpoint registration failed");
        return;
    }

    ezb_app_signal_add_handler(app_signal_handler);
    zigbee_gateway_register_zcl_handlers();
    ezb_secur_tcpol_set_allow_rejoins_with_well_known_key(true);
    if (esp_zigbee_start(false) != ESP_OK) {
        fail_zigbee_task("esp_zigbee_start failed");
        return;
    }
    set_stack_ready(true);
    network_event(GATEWAY_EVENT_STACK_READY);
    if (!zigbee_gateway_work_start()) {
        gateway_event_warning(NULL, "discovery task creation failed; discovery disabled");
    }
    esp_zigbee_launch_mainloop();
}

esp_err_t zigbee_gateway_start(void)
{
    return xTaskCreate(zigbee_task, "zigbee_main", 6144, NULL, 5, NULL) == pdPASS ?
        ESP_OK : ESP_ERR_NO_MEM;
}

zigbee_gateway_command_submit_result_t zigbee_gateway_submit_command(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_command_kind_t kind,
    double value,
    uint32_t transition_ms)
{
    if (request_id == 0U || input == NULL) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    if (input->source != GATEWAY_SOURCE_ZIGBEE) {
        return ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED;
    }
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (!gateway_zigbee_parse_input_identity(input, ieee, &endpoint)) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    gateway_command_plan_t plan;
    const gateway_command_plan_result_t plan_result = gateway_command_policy_plan(
        kind, value, transition_ms, &plan);
    if (plan_result == GATEWAY_COMMAND_PLAN_UNSUPPORTED) {
        return ZIGBEE_GATEWAY_COMMAND_UNSUPPORTED;
    }
    if (plan_result != GATEWAY_COMMAND_PLAN_OK) {
        return ZIGBEE_GATEWAY_COMMAND_INVALID;
    }
    if (!stack_is_ready()) {
        return ZIGBEE_GATEWAY_COMMAND_ERROR;
    }
    return zigbee_gateway_enqueue_command_request(request_id, input, &plan) ?
        ZIGBEE_GATEWAY_COMMAND_QUEUED : ZIGBEE_GATEWAY_COMMAND_ERROR;
}

zigbee_gateway_policy_submit_result_t zigbee_gateway_set_measurement_policy(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    uint32_t min_interval_ms,
    uint32_t max_interval_ms,
    double reportable_change)
{
    if (request_id == 0U || input == NULL ||
        input->source != GATEWAY_SOURCE_ZIGBEE) {
        return ZIGBEE_GATEWAY_POLICY_UNSUPPORTED;
    }
    gateway_reporting_plan_t plan;
    const gateway_reporting_plan_result_t plan_result = gateway_reporting_policy_plan(
        kind, min_interval_ms, max_interval_ms, reportable_change, &plan);
    if (plan_result == GATEWAY_REPORTING_PLAN_UNSUPPORTED) {
        return ZIGBEE_GATEWAY_POLICY_UNSUPPORTED;
    }
    if (plan_result == GATEWAY_REPORTING_PLAN_INVALID || !stack_is_ready()) {
        return ZIGBEE_GATEWAY_POLICY_ERROR;
    }
    return zigbee_gateway_enqueue_reporting_request(
            request_id, input, kind, &plan) ?
        ZIGBEE_GATEWAY_POLICY_QUEUED : ZIGBEE_GATEWAY_POLICY_ERROR;
}

esp_err_t zigbee_gateway_set_permit_join(uint8_t seconds)
{
    if (!stack_is_ready()) {
        gateway_event_warning(NULL, "permit command rejected: Zigbee stack not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (!esp_zigbee_lock_acquire(
            pdMS_TO_TICKS(ZIGBEE_GATEWAY_LOCK_TIMEOUT_MS))) {
        gateway_event_warning(NULL, "permit command rejected: Zigbee lock timeout");
        return ESP_ERR_TIMEOUT;
    }
    if (seconds == 0U) {
        ezb_bdb_close_network();
    } else {
        ezb_bdb_open_network(seconds);
    }
    esp_zigbee_lock_release();
    return ESP_OK;
}
