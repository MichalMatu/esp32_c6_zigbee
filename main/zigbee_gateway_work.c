#include "zigbee_gateway_internal.h"

#include <string.h>

#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <ezbee/af.h>
#include <ezbee/zcl/zcl_general_cmd.h>
#include <ezbee/zdo/zdo_bind_mgmt.h>
#include <ezbee/zdo/zdo_dev_srv_disc.h>

#include "gateway_zigbee_input.h"

#define GATEWAY_DISCOVERY_QUEUE_DEPTH 16U
#define GATEWAY_MAX_ASYNC_CONTEXTS 32U
#define GATEWAY_DISCOVERY_MAX_RETRIES 3U
#define GATEWAY_DISCOVERY_RETRY_DELAY_MS 50U

#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U
#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U
#define ZCL_ATTR_IAS_ZONE_TYPE 0x0001U
#define ZCL_ATTR_IAS_CIE_ADDRESS 0x0010U
#define ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH 0x0015U

typedef enum {
    DISCOVERY_ACTIVE_ENDPOINTS,
    DISCOVERY_SIMPLE_DESCRIPTOR,
    DISCOVERY_READ_BASIC,
    DISCOVERY_READ_IAS_ZONE_TYPE,
    DISCOVERY_WRITE_IAS_CIE,
    DISCOVERY_BIND_CLUSTER,
    DISCOVERY_CONFIG_REPORTING,
    DISCOVERY_EXTERNAL_REPORTING,
    DISCOVERY_EXTERNAL_COMMAND,
} discovery_kind_t;

typedef struct {
    discovery_kind_t kind;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint8_t retry_count;
    bool reporting_spec_valid;
    gateway_reporting_spec_t reporting_spec;
    uint32_t config_request_id;
    bool config_clamped;
    gateway_input_id_t external_input;
    gateway_measurement_kind_t external_kind;
    gateway_command_plan_t command_plan;
    uint32_t command_request_id;
} discovery_job_t;

typedef struct {
    bool in_use;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint8_t retry_count;
} async_context_t;


static async_context_t s_async_contexts[GATEWAY_MAX_ASYNC_CONTEXTS];
static StaticQueue_t s_discovery_queue_storage;
static uint8_t s_discovery_queue_buffer[
    GATEWAY_DISCOVERY_QUEUE_DEPTH * sizeof(discovery_job_t)
];
static QueueHandle_t s_discovery_queue;
endpoint_state_t *zigbee_gateway_endpoint_state(
    device_slot_t *slot, uint8_t endpoint, bool create)
{
    endpoint_state_t *state = gateway_device_endpoint_state(slot, endpoint, create);
    if (state == NULL && create && slot != NULL) {
        gateway_event_warning(&slot->device, "endpoint state capacity exhausted");
    }
    return state;
}

static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries
);
bool zigbee_gateway_schedule_active_discovery(device_slot_t *slot)
{
    if (!gateway_device_claim_discovery(slot)) {
        return false;
    }
    const uint16_t short_addr = slot->device.short_addr;
    if (queue_job(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U, 0U, 0U)) {
        return true;
    }
    gateway_device_release_discovery(slot, short_addr);
    return false;
}

static bool enqueue_device_job(device_slot_t *slot, discovery_job_t job)
{
    if (s_discovery_queue == NULL || slot == NULL || slot->state != SLOT_ACTIVE ||
        slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        return false;
    }
    job.device = gateway_device_ref_for(slot);
    job.route_short_addr = slot->device.short_addr;
    if (xQueueSend(s_discovery_queue, &job, 0) != pdPASS) {
        return false;
    }
    ++slot->pending_jobs;
    return true;
}

static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries)
{
    return enqueue_device_job(slot, (discovery_job_t){
        .kind = kind,
        .endpoint = endpoint,
        .cluster_id = cluster,
        .retry_count = retries,
    });
}

static bool queue_reporting_job(
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    const gateway_reporting_spec_t *spec,
    uint32_t request_id,
    bool clamped,
    uint8_t retries)
{
    if (spec == NULL) {
        return false;
    }
    return enqueue_device_job(slot, (discovery_job_t){
        .kind = DISCOVERY_CONFIG_REPORTING,
        .endpoint = endpoint,
        .cluster_id = cluster,
        .retry_count = retries,
        .reporting_spec_valid = true,
        .reporting_spec = *spec,
        .config_request_id = request_id,
        .config_clamped = clamped,
    });
}

static bool schedule_basic(device_slot_t *slot, uint8_t endpoint)
{
    endpoint_state_t *state = zigbee_gateway_endpoint_state(slot, endpoint, true);
    if (state == NULL || state->basic_state != BASIC_NOT_SCHEDULED) {
        return false;
    }
    if (!queue_job(DISCOVERY_READ_BASIC, slot, endpoint, 0U, 0U)) {
        gateway_event_warning(&slot->device, "Basic read queue full");
        return false;
    }
    state->basic_state = BASIC_SCHEDULED;
    state->basic_scheduled_at_ms = gateway_uptime_ms();
    return true;
}

static bool schedule_ias_zone_type(device_slot_t *slot, uint8_t endpoint)
{
    endpoint_state_t *state = zigbee_gateway_endpoint_state(slot, endpoint, true);
    if (state == NULL || state->ias_zone_type_known ||
        state->ias_zone_type_read_requested) {
        return false;
    }
    if (!queue_job(
            DISCOVERY_READ_IAS_ZONE_TYPE, slot, endpoint,
            ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE, 0U)) {
        gateway_event_warning(&slot->device, "IAS ZoneType read queue full");
        return false;
    }
    state->ias_zone_type_read_requested = true;
    state->ias_zone_type_requested_at_ms = gateway_uptime_ms();
    return true;
}

static bool schedule_binding(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    if (!gateway_reporting_policy_requires_binding(cluster)) {
        return false;
    }
    endpoint_state_t *state = zigbee_gateway_endpoint_state(slot, endpoint, true);
    if (state == NULL) {
        return false;
    }
    binding_state_t *binding = gateway_device_binding_state(
        slot, endpoint, cluster, true);
    if (binding == NULL) {
        gateway_event_warning(&slot->device, "binding state capacity exhausted");
        return false;
    }
    if (binding->requested || binding->configured) {
        return false;
    }
    if (!queue_job(DISCOVERY_BIND_CLUSTER, slot, endpoint, cluster, 0U)) {
        gateway_event_warning(&slot->device, "binding queue full");
        return false;
    }
    binding->requested = true;
    binding->requested_at_ms = gateway_uptime_ms();
    binding->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    return true;
}

static bool schedule_reporting(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    gateway_reporting_spec_t spec;
    if (!gateway_reporting_policy_spec(cluster, &spec)) {
        return false;
    }
    endpoint_state_t *state = zigbee_gateway_endpoint_state(slot, endpoint, true);
    if (state == NULL) {
        return false;
    }
    if (gateway_reporting_policy_requires_binding(cluster)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, endpoint, cluster, false);
        if (binding == NULL || !binding->configured) {
            return false;
        }
    }
    reporting_state_t *reporting = gateway_device_reporting_state(
        slot, endpoint, cluster, spec.attribute_id, true);
    if (reporting == NULL) {
        gateway_event_warning(&slot->device, "reporting state capacity exhausted");
        return false;
    }
    if (reporting->requested || reporting->configured) {
        return false;
    }
    if (!queue_reporting_job(slot, endpoint, cluster, &spec, 0U, false, 0U)) {
        gateway_event_warning(&slot->device, "reporting queue full");
        return false;
    }
    reporting->requested = true;
    reporting->requested_at_ms = gateway_uptime_ms();
    reporting->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    return true;
}

static async_context_t *context_alloc(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster, uint8_t retries)
{
    for (size_t i = 0; i < GATEWAY_MAX_ASYNC_CONTEXTS; ++i) {
        if (!s_async_contexts[i].in_use) {
            s_async_contexts[i] = (async_context_t){
                .in_use = true,
                .device = gateway_device_ref_for(slot),
                .route_short_addr = slot->device.short_addr,
                .endpoint = endpoint,
                .cluster_id = cluster,
                .retry_count = retries,
            };
            ++slot->pending_requests;
            return &s_async_contexts[i];
        }
    }
    return NULL;
}

static void context_release(async_context_t *context)
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

static void clear_pending(device_slot_t *slot, const discovery_job_t *job)
{
    endpoint_state_t *state = zigbee_gateway_endpoint_state(slot, job->endpoint, false);
    if (job->kind == DISCOVERY_READ_BASIC && state != NULL &&
        state->basic_state == BASIC_SCHEDULED) {
        state->basic_state = BASIC_NOT_SCHEDULED;
    }
    if (job->kind == DISCOVERY_READ_IAS_ZONE_TYPE && state != NULL) {
        state->ias_zone_type_read_requested = false;
    }
    if (job->kind == DISCOVERY_BIND_CLUSTER) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, job->endpoint, job->cluster_id, false);
        if (binding != NULL) {
            binding->requested = false;
        }
    }
    if (job->kind == DISCOVERY_CONFIG_REPORTING && job->reporting_spec_valid) {
        reporting_state_t *reporting = gateway_device_reporting_state(
            slot, job->endpoint, job->cluster_id,
            job->reporting_spec.attribute_id, false);
        if (reporting != NULL) {
            reporting->requested = false;
        }
    }
}

void zigbee_gateway_publish_reporting_result(
    const gateway_device_id_t *device,
    uint8_t endpoint,
    uint16_t cluster_id,
    uint16_t attribute_id,
    uint8_t zcl_status,
    uint32_t request_id,
    gateway_event_config_result_t result)
{
    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_REPORTING_CONFIG, device);
    event.endpoint = endpoint;
    event.data.reporting.cluster_id = cluster_id;
    event.data.reporting.attribute_id = attribute_id;
    event.data.reporting.status = zcl_status;
    event.data.reporting.request_id = request_id;
    event.data.reporting.result = result;
    gateway_event_publish(&event);
}

static void fail_external_reporting_job(
    device_slot_t *slot, const discovery_job_t *job, uint8_t status)
{
    if (job == NULL || job->config_request_id == 0U || !job->reporting_spec_valid) {
        return;
    }
    if (slot != NULL) {
        reporting_state_t *reporting = gateway_device_reporting_state(
            slot, job->endpoint, job->cluster_id,
            job->reporting_spec.attribute_id, false);
        if (reporting != NULL && reporting->request_id == job->config_request_id) {
            reporting->requested = false;
            reporting->request_id = 0U;
            reporting->request_clamped = false;
        }
    }
    zigbee_gateway_publish_reporting_result(
        slot == NULL ? NULL : &slot->device,
        job->endpoint, job->cluster_id, job->reporting_spec.attribute_id,
        status, job->config_request_id, GATEWAY_EVENT_CONFIG_ERROR);
}

static void retry_or_fail(discovery_job_t job, const char *message)
{
    device_slot_t *slot = gateway_device_from_ref(job.device, true);
    if (slot == NULL) {
        return;
    }
    if (gateway_device_route_is_current(slot, job.route_short_addr) &&
        job.retry_count < GATEWAY_DISCOVERY_MAX_RETRIES &&
        enqueue_device_job(
            slot,
            (discovery_job_t){
                .kind = job.kind,
                .endpoint = job.endpoint,
                .cluster_id = job.cluster_id,
                .retry_count = (uint8_t)(job.retry_count + 1U),
                .reporting_spec_valid = job.reporting_spec_valid,
                .reporting_spec = job.reporting_spec,
                .config_request_id = job.config_request_id,
                .config_clamped = job.config_clamped,
            })) {
        return;
    }
    clear_pending(slot, &job);
    if (job.kind == DISCOVERY_CONFIG_REPORTING) {
        fail_external_reporting_job(slot, &job, GATEWAY_CONFIG_STATUS_UNKNOWN);
    }
    if (job.kind == DISCOVERY_ACTIVE_ENDPOINTS) {
        gateway_device_release_discovery(slot, job.route_short_addr);
    }
    gateway_event_warning(&slot->device, message);
    gateway_device_maybe_reclaim(slot);
}


static bool context_route(const async_context_t *context, device_slot_t **out)
{
    device_slot_t *slot = context == NULL ? NULL : gateway_device_from_ref(context->device, false);
    if (!gateway_device_route_is_current(slot, context->route_short_addr)) {
        return false;
    }
    *out = slot;
    return true;
}

static void binding_callback(const ezb_zdp_bind_req_result_t *result, void *user_ctx)
{
    async_context_t *context = user_ctx;
    device_slot_t *slot = NULL;
    const uint8_t status =
        result != NULL && result->error == EZB_ERR_NONE && result->rsp != NULL ?
        result->rsp->status : 0xffU;

    if (context != NULL && context->in_use && context_route(context, &slot)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, context->endpoint, context->cluster_id, false);
        if (binding != NULL) {
            binding->last_status = status;
        }
        if (status == EZB_ZDP_STATUS_SUCCESS && binding != NULL) {
            binding->requested = false;
            binding->configured = true;
            schedule_reporting(slot, context->endpoint, context->cluster_id);
        } else if (binding != NULL) {
            retry_or_fail(
                (discovery_job_t){
                    .kind = DISCOVERY_BIND_CLUSTER,
                    .device = context->device,
                    .route_short_addr = context->route_short_addr,
                    .endpoint = context->endpoint,
                    .cluster_id = context->cluster_id,
                    .retry_count = context->retry_count,
                },
                "binding failed after retries"
            );
        }
        gateway_event_t event = gateway_event_make(
            GATEWAY_EVENT_BINDING, &slot->device
        );
        event.endpoint = context->endpoint;
        event.data.binding.cluster_id = context->cluster_id;
        event.data.binding.status = status;
        gateway_event_publish(&event);
    }
    context_release(context);
}

static void simple_callback(
    const ezb_zdo_simple_desc_req_result_t *result, void *user_ctx)
{
    async_context_t *context = user_ctx;
    device_slot_t *slot = NULL;
    if (context == NULL || !context->in_use || !context_route(context, &slot) ||
        result == NULL || result->error != EZB_ERR_NONE || result->rsp == NULL ||
        result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        if (context != NULL && context->in_use) {
            retry_or_fail(
                (discovery_job_t){
                    .kind = DISCOVERY_SIMPLE_DESCRIPTOR,
                    .device = context->device,
                    .route_short_addr = context->route_short_addr,
                    .endpoint = context->endpoint,
                    .retry_count = context->retry_count,
                },
                "simple descriptor failed after retries"
            );
            context_release(context);
        }
        return;
    }

    const ezb_af_simple_desc_t *desc = &result->rsp->desc;
    endpoint_state_t *input_state = zigbee_gateway_endpoint_state(slot, desc->ep_id, true);
    gateway_input_capability_profile_t profile =
        gateway_zigbee_capability_profile_from_clusters(
            desc->app_cluster_list, desc->app_input_cluster_count);
    if (input_state != NULL && input_state->ias_zone_type_known &&
        input_state->ias_zone_type == ZCL_IAS_ZONE_TYPE_CONTACT_SWITCH) {
        profile.readable |= GATEWAY_INPUT_CAP_CONTACT_OPEN;
    }
    if (!slot->device.ieee_valid) {
        ezb_extaddr_t resolved_ieee;
        if (ezb_address_extended_by_short(slot->device.short_addr, &resolved_ieee) ==
            EZB_ERR_NONE) {
            memcpy(slot->device.ieee, resolved_ieee.u8, sizeof(slot->device.ieee));
            slot->device.ieee_valid = true;
        }
    }
    if (input_state != NULL && input_state->input_announced && profile.readable == 0U) {
        (void)zigbee_gateway_publish_input(slot, input_state, false);
    }
    if (input_state != NULL) {
        input_state->input_profile = profile;
    }

    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_ENDPOINT, &slot->device);
    event.endpoint = desc->ep_id;
    event.data.endpoint_desc.profile_id = desc->app_profile_id;
    event.data.endpoint_desc.device_id = desc->app_device_id;
    event.data.endpoint_desc.input_count = desc->app_input_cluster_count;
    event.data.endpoint_desc.output_count = desc->app_output_cluster_count;
    event.data.endpoint_desc.input_copied =
        desc->app_input_cluster_count > GATEWAY_MAX_DESCRIPTOR_CLUSTERS ?
        GATEWAY_MAX_DESCRIPTOR_CLUSTERS : desc->app_input_cluster_count;
    event.data.endpoint_desc.output_copied =
        desc->app_output_cluster_count > GATEWAY_MAX_DESCRIPTOR_CLUSTERS ?
        GATEWAY_MAX_DESCRIPTOR_CLUSTERS : desc->app_output_cluster_count;

    for (uint8_t i = 0; i < event.data.endpoint_desc.input_copied; ++i) {
        event.data.endpoint_desc.input_clusters[i] = desc->app_cluster_list[i];
    }
    for (uint8_t i = 0; i < event.data.endpoint_desc.output_copied; ++i) {
        event.data.endpoint_desc.output_clusters[i] =
            desc->app_cluster_list[desc->app_input_cluster_count + i];
    }
    gateway_event_publish(&event);
    if (input_state != NULL && profile.readable != 0U) {
        (void)zigbee_gateway_publish_input(slot, input_state, true);
    }

    bool basic = false;
    bool ias_zone = false;
    for (uint8_t i = 0; i < desc->app_input_cluster_count; ++i) {
        const uint16_t cluster = desc->app_cluster_list[i];
        if (cluster == EZB_ZCL_CLUSTER_ID_BASIC) {
            basic = true;
        }
        if (cluster == ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE) {
            ias_zone = true;
        }
        if (gateway_reporting_policy_requires_binding(cluster)) {
            schedule_binding(slot, desc->ep_id, cluster);
        } else {
            gateway_reporting_spec_t spec;
            if (gateway_reporting_policy_spec(cluster, &spec)) {
                schedule_reporting(slot, desc->ep_id, cluster);
            }
        }
    }
    if (basic) {
        schedule_basic(slot, desc->ep_id);
    }
    if (ias_zone) {
        (void)schedule_ias_zone_type(slot, desc->ep_id);
    }
    context_release(context);
}

static void active_callback(
    const ezb_zdo_active_ep_req_result_t *result, void *user_ctx)
{
    async_context_t *context = user_ctx;
    device_slot_t *slot = NULL;
    if (context == NULL || !context->in_use || !context_route(context, &slot) ||
        result == NULL || result->error != EZB_ERR_NONE || result->rsp == NULL ||
        result->rsp->status != EZB_ZDP_STATUS_SUCCESS) {
        if (context != NULL && context->in_use) {
            retry_or_fail(
                (discovery_job_t){
                    .kind = DISCOVERY_ACTIVE_ENDPOINTS,
                    .device = context->device,
                    .route_short_addr = context->route_short_addr,
                    .retry_count = context->retry_count,
                },
                "active endpoint discovery failed after retries"
            );
            context_release(context);
        }
        return;
    }

    for (uint8_t i = 0; i < result->rsp->active_ep_count; ++i) {
        if (!queue_job(
                DISCOVERY_SIMPLE_DESCRIPTOR,
                slot,
                result->rsp->active_ep_list[i],
                0U,
                0U)) {
            gateway_event_warning(&slot->device, "simple descriptor queue full");
        }
    }
    context_release(context);
}

static bool submit_active(device_slot_t *slot, const discovery_job_t *job)
{
    async_context_t *context = context_alloc(slot, 0U, 0U, job->retry_count);
    if (context == NULL) {
        return false;
    }
    const ezb_zdo_active_ep_req_t request = {
        .dst_nwk_addr = slot->device.short_addr,
        .field = {.nwk_addr_of_interest = slot->device.short_addr},
        .cb = active_callback,
        .user_ctx = context,
    };
    if (ezb_zdo_active_ep_req(&request) == EZB_ERR_NONE) {
        return true;
    }
    context_release(context);
    return false;
}

static bool submit_simple(device_slot_t *slot, const discovery_job_t *job)
{
    async_context_t *context = context_alloc(
        slot, job->endpoint, 0U, job->retry_count
    );
    if (context == NULL) {
        return false;
    }
    const ezb_zdo_simple_desc_req_t request = {
        .dst_nwk_addr = slot->device.short_addr,
        .field = {
            .nwk_addr_of_interest = slot->device.short_addr,
            .endpoint = job->endpoint,
        },
        .cb = simple_callback,
        .user_ctx = context,
    };
    if (ezb_zdo_simple_desc_req(&request) == EZB_ERR_NONE) {
        return true;
    }
    context_release(context);
    return false;
}

static bool submit_basic(device_slot_t *slot, const discovery_job_t *job)
{
    uint16_t attrs[] = {
        ZCL_ATTR_BASIC_MANUFACTURER_NAME,
        ZCL_ATTR_BASIC_MODEL_IDENTIFIER,
    };
    const ezb_zcl_read_attr_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
            .cluster_id = EZB_ZCL_CLUSTER_ID_BASIC,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.attr_number = 2U, .attr_field = attrs},
    };
    return ezb_zcl_read_attr_cmd_req(&request) == EZB_ERR_NONE;
}

static bool submit_ias_zone_type(device_slot_t *slot, const discovery_job_t *job)
{
    uint16_t attrs[] = {ZCL_ATTR_IAS_ZONE_TYPE};
    const ezb_zcl_read_attr_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
            .cluster_id = ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.attr_number = 1U, .attr_field = attrs},
    };
    return ezb_zcl_read_attr_cmd_req(&request) == EZB_ERR_NONE;
}

static bool submit_ias_cie(device_slot_t *slot, const discovery_job_t *job)
{
    ezb_extaddr_t coordinator;
    ezb_nwk_get_extended_address(&coordinator);
    ezb_zcl_attribute_t attr = {
        .id = ZCL_ATTR_IAS_CIE_ADDRESS,
        .data = {
            .type = EZB_ZCL_ATTR_TYPE_EUI64,
            .size = sizeof(coordinator.u8),
            .value = coordinator.u8,
        },
    };
    const ezb_zcl_write_attr_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
            .cluster_id = ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.attr_number = 1U, .attr_field = &attr},
    };
    return ezb_zcl_write_attr_cmd_req(&request) == EZB_ERR_NONE;
}

static bool submit_binding(device_slot_t *slot, const discovery_job_t *job)
{
    if (!slot->device.ieee_valid) {
        ezb_extaddr_t ieee;
        if (ezb_address_extended_by_short(slot->device.short_addr, &ieee) !=
            EZB_ERR_NONE) {
            return false;
        }
        memcpy(slot->device.ieee, ieee.u8, sizeof(slot->device.ieee));
        slot->device.ieee_valid = true;
    }

    async_context_t *context = context_alloc(
        slot, job->endpoint, job->cluster_id, job->retry_count
    );
    if (context == NULL) {
        return false;
    }

    ezb_extaddr_t coordinator;
    ezb_nwk_get_extended_address(&coordinator);
    ezb_zdo_bind_req_t request = {0};
    request.dst_nwk_addr = slot->device.short_addr;
    memcpy(
        request.field.src_addr.u8,
        slot->device.ieee,
        sizeof(slot->device.ieee)
    );
    request.field.src_ep = job->endpoint;
    request.field.cluster_id = job->cluster_id;
    request.field.dst_addr_mode = EZB_ADDR_MODE_EXT;
    request.field.dst_addr.extended_addr = coordinator;
    request.field.dst_ep = ZIGBEE_GATEWAY_ENDPOINT;
    request.cb = binding_callback;
    request.user_ctx = context;
    if (ezb_zdo_bind_req(&request) == EZB_ERR_NONE) {
        return true;
    }
    context_release(context);
    return false;
}

static bool submit_reporting(device_slot_t *slot, const discovery_job_t *job)
{
    if (job == NULL || !job->reporting_spec_valid) {
        return false;
    }
    const gateway_reporting_spec_t *spec = &job->reporting_spec;

    ezb_zcl_config_report_record_t record = {
        .direction = EZB_ZCL_REPORTING_SEND,
        .attr_id = spec->attribute_id,
    };
    record.client.attr_type = spec->attribute_type;
    record.client.min_interval = spec->min_interval;
    record.client.max_interval = spec->max_interval;
    switch (spec->change_kind) {
    case GATEWAY_REPORTING_CHANGE_S16:
        record.client.reportable_change.s16 = (int16_t)spec->reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U16:
        record.client.reportable_change.u16 = (uint16_t)spec->reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U8:
        record.client.reportable_change.u8 = (uint8_t)spec->reportable_change;
        break;
    default:
        return false;
    }

    const ezb_zcl_config_report_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = ZIGBEE_GATEWAY_ENDPOINT,
            .cluster_id = job->cluster_id,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.record_number = 1U, .record_field = &record},
    };
    return ezb_zcl_config_report_cmd_req(&request) == EZB_ERR_NONE;
}

static void reject_external_reporting(
    const discovery_job_t *job, gateway_event_config_result_t result)
{
    if (job == NULL || job->config_request_id == 0U) {
        return;
    }
    zigbee_gateway_publish_reporting_result(
        NULL, job->external_input.channel, job->cluster_id,
        job->reporting_spec.attribute_id, GATEWAY_CONFIG_STATUS_UNKNOWN,
        job->config_request_id, result);
}

static void handle_external_reporting(const discovery_job_t *external)
{
    uint8_t ieee[8];
    uint8_t endpoint = 0U;
    if (external == NULL || !external->reporting_spec_valid ||
        !gateway_zigbee_parse_input_identity(&external->external_input, ieee, &endpoint)) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    device_slot_t *slot = gateway_device_find_by_ieee(ieee, false);
    endpoint_state_t *ep_state = slot == NULL ? NULL : zigbee_gateway_endpoint_state(slot, endpoint, false);
    const gateway_input_capabilities_t capability =
        gateway_input_capability_for_measurement(external->external_kind);
    if (slot == NULL || ep_state == NULL) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    if (capability == 0U || (ep_state->input_profile.configurable & capability) == 0U) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_UNSUPPORTED);
        return;
    }
    if (gateway_reporting_policy_requires_binding(external->cluster_id)) {
        binding_state_t *binding = gateway_device_binding_state(
            slot, endpoint, external->cluster_id, false);
        if (binding == NULL || !binding->configured) {
            reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
            return;
        }
    }
    reporting_state_t *reporting = gateway_device_reporting_state(
        slot, endpoint, external->cluster_id,
        external->reporting_spec.attribute_id, true);
    if (reporting == NULL || reporting->requested) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    if (!queue_reporting_job(
            slot, endpoint, external->cluster_id, &external->reporting_spec,
            external->config_request_id, external->config_clamped, 0U)) {
        reject_external_reporting(external, GATEWAY_EVENT_CONFIG_ERROR);
        return;
    }
    reporting->requested = true;
    reporting->requested_at_ms = gateway_uptime_ms();
    reporting->last_status = GATEWAY_CONFIG_STATUS_UNKNOWN;
    reporting->request_id = external->config_request_id;
    reporting->request_clamped = external->config_clamped;
}

static void discovery_task(void *arg)
{
    (void)arg;
    discovery_job_t job;
    for (;;) {
        if (xQueueReceive(s_discovery_queue, &job, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (job.kind == DISCOVERY_EXTERNAL_REPORTING) {
            handle_external_reporting(&job);
            continue;
        }
        if (job.kind == DISCOVERY_EXTERNAL_COMMAND) {
            zigbee_gateway_execute_command(
                job.command_request_id, &job.external_input, &job.command_plan);
            continue;
        }
        device_slot_t *slot = gateway_device_from_ref(job.device, true);
        if (slot == NULL) {
            continue;
        }
        if (slot->pending_jobs != 0U) {
            --slot->pending_jobs;
        }
        if (!gateway_device_route_is_current(slot, job.route_short_addr)) {
            clear_pending(slot, &job);
            if (job.kind == DISCOVERY_CONFIG_REPORTING) {
                fail_external_reporting_job(slot, &job, GATEWAY_CONFIG_STATUS_UNKNOWN);
            }
            gateway_event_warning(&slot->device, "discovery cancelled: route superseded");
            gateway_device_maybe_reclaim(slot);
            continue;
        }
        if (job.retry_count != 0U) {
            vTaskDelay(pdMS_TO_TICKS(
                GATEWAY_DISCOVERY_RETRY_DELAY_MS * job.retry_count
            ));
        }
        if (!esp_zigbee_lock_acquire(
                pdMS_TO_TICKS(ZIGBEE_GATEWAY_LOCK_TIMEOUT_MS))) {
            retry_or_fail(job, "discovery lock timeout after retries");
            continue;
        }
        const bool sent =
            job.kind == DISCOVERY_ACTIVE_ENDPOINTS ? submit_active(slot, &job) :
            job.kind == DISCOVERY_SIMPLE_DESCRIPTOR ? submit_simple(slot, &job) :
            job.kind == DISCOVERY_READ_BASIC ? submit_basic(slot, &job) :
            job.kind == DISCOVERY_READ_IAS_ZONE_TYPE ? submit_ias_zone_type(slot, &job) :
            job.kind == DISCOVERY_WRITE_IAS_CIE ? submit_ias_cie(slot, &job) :
            job.kind == DISCOVERY_BIND_CLUSTER ? submit_binding(slot, &job) :
            submit_reporting(slot, &job);
        esp_zigbee_lock_release();
        if (!sent) {
            retry_or_fail(job, "discovery submit failed after retries");
        }
    }
}


bool zigbee_gateway_work_init(void)
{
    if (s_discovery_queue != NULL) {
        return true;
    }
    s_discovery_queue = xQueueCreateStatic(
        GATEWAY_DISCOVERY_QUEUE_DEPTH,
        sizeof(discovery_job_t),
        s_discovery_queue_buffer,
        &s_discovery_queue_storage);
    return s_discovery_queue != NULL;
}

bool zigbee_gateway_work_start(void)
{
    return s_discovery_queue != NULL &&
        xTaskCreate(discovery_task, "zb_discovery", 4096, NULL, 5, NULL) == pdPASS;
}

bool zigbee_gateway_schedule_ias_cie(device_slot_t *slot, uint8_t endpoint)
{
    return queue_job(
        DISCOVERY_WRITE_IAS_CIE, slot, endpoint,
        ZIGBEE_GATEWAY_CLUSTER_IAS_ZONE, 0U);
}

bool zigbee_gateway_enqueue_reporting_request(
    uint32_t request_id,
    const gateway_input_id_t *input,
    gateway_measurement_kind_t kind,
    const gateway_reporting_plan_t *plan)
{
    if (s_discovery_queue == NULL || request_id == 0U || input == NULL ||
        plan == NULL) {
        return false;
    }
    const discovery_job_t job = {
        .kind = DISCOVERY_EXTERNAL_REPORTING,
        .cluster_id = plan->cluster_id,
        .reporting_spec_valid = true,
        .reporting_spec = plan->spec,
        .config_request_id = request_id,
        .config_clamped = plan->clamped,
        .external_input = *input,
        .external_kind = kind,
    };
    return xQueueSend(s_discovery_queue, &job, 0U) == pdPASS;
}

bool zigbee_gateway_enqueue_command_request(
    uint32_t request_id,
    const gateway_input_id_t *input,
    const gateway_command_plan_t *plan)
{
    if (s_discovery_queue == NULL || request_id == 0U || input == NULL ||
        plan == NULL) {
        return false;
    }
    const discovery_job_t job = {
        .kind = DISCOVERY_EXTERNAL_COMMAND,
        .external_input = *input,
        .command_plan = *plan,
        .command_request_id = request_id,
    };
    return xQueueSend(s_discovery_queue, &job, 0U) == pdPASS;
}
