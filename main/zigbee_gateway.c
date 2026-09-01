#include "zigbee_gateway.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <ezbee/af.h>
#include <ezbee/app_signals.h>
#include <ezbee/bdb.h>
#include <ezbee/secur.h>
#include <ezbee/zcl/cluster/poll_control.h>
#include <ezbee/zcl/zcl_core.h>
#include <ezbee/zcl/zcl_general_cmd.h>
#include <ezbee/zdo/zdo_bind_mgmt.h>
#include <ezbee/zdo/zdo_dev_srv_disc.h>

#include "gateway_events.h"
#include "gateway_reporting_policy.h"
#include "gateway_zcl_value.h"

#define GATEWAY_ENDPOINT 1U
#define GATEWAY_PROFILE_ID 0x0104U
#define GATEWAY_DEVICE_ID 0x0000U
#define GATEWAY_CHANNEL_MASK 0x07fff800UL
#define GATEWAY_MAX_DEVICES 16U
#define GATEWAY_MAX_ENDPOINTS_PER_DEVICE 8U
#define GATEWAY_DISCOVERY_QUEUE_DEPTH 16U
#define GATEWAY_MAX_ASYNC_CONTEXTS 32U
#define GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS 100U
#define GATEWAY_DISCOVERY_MAX_RETRIES 3U
#define GATEWAY_DISCOVERY_RETRY_DELAY_MS 50U
#define GATEWAY_REQUEST_STALE_MS 10000U
#define GATEWAY_INVALID_SHORT_ADDR 0xffffU

/* Poll Control Check-In uses quarter-seconds, not the millisecond SDK macro. */
#define GATEWAY_FAST_POLL_TIMEOUT_QUARTER_SECONDS 20U /* five seconds */

#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U
#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U

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
} endpoint_state_t;

typedef struct {
    slot_state_t state;
    uint32_t generation;
    gateway_device_id_t device;
    ezb_shortaddr_t previous_short_addr;
    uint8_t pending_jobs;
    uint8_t pending_requests;
    endpoint_state_t endpoints[GATEWAY_MAX_ENDPOINTS_PER_DEVICE];
} device_slot_t;

typedef struct {
    uint8_t index;
    uint32_t generation;
} device_ref_t;

typedef enum {
    DISCOVERY_ACTIVE_ENDPOINTS,
    DISCOVERY_SIMPLE_DESCRIPTOR,
    DISCOVERY_READ_BASIC,
    DISCOVERY_BIND_CLUSTER,
    DISCOVERY_CONFIG_REPORTING,
} discovery_kind_t;

typedef struct {
    discovery_kind_t kind;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint8_t retry_count;
} discovery_job_t;

typedef struct {
    bool in_use;
    device_ref_t device;
    ezb_shortaddr_t route_short_addr;
    uint8_t endpoint;
    uint16_t cluster_id;
    uint8_t retry_count;
} async_context_t;

static device_slot_t s_devices[GATEWAY_MAX_DEVICES];
static async_context_t s_async_contexts[GATEWAY_MAX_ASYNC_CONTEXTS];
static StaticQueue_t s_discovery_queue_storage;
static uint8_t s_discovery_queue_buffer[
    GATEWAY_DISCOVERY_QUEUE_DEPTH * sizeof(discovery_job_t)
];
static QueueHandle_t s_discovery_queue;

static gateway_event_t event_base(
    gateway_event_kind_t kind, const gateway_device_id_t *device)
{
    gateway_event_t event = {
        .source = GATEWAY_SOURCE_ZIGBEE,
        .kind = kind,
        .uptime_ms = gateway_uptime_ms(),
    };
    if (device != NULL) {
        event.device = *device;
    }
    return event;
}

static void warning(const gateway_device_id_t *device, const char *text)
{
    gateway_event_t event = event_base(GATEWAY_EVENT_WARNING, device);
    strncpy(event.data.text.value, text, sizeof(event.data.text.value) - 1U);
    gateway_event_publish(&event);
}

static bool ieee_matches(const device_slot_t *slot, const ezb_extaddr_t *ieee)
{
    return ieee != NULL && slot->device.ieee_valid &&
        memcmp(slot->device.ieee, ieee->u8, sizeof(slot->device.ieee)) == 0;
}

static device_slot_t *find_by_ieee(const ezb_extaddr_t *ieee, bool include_leaving)
{
    for (size_t i = 0; ieee != NULL && i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].state != SLOT_EMPTY &&
            (include_leaving || s_devices[i].state == SLOT_ACTIVE) &&
            ieee_matches(&s_devices[i], ieee)) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static device_slot_t *find_by_short(
    ezb_shortaddr_t short_addr, bool include_leaving)
{
    for (size_t i = 0; i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].state != SLOT_EMPTY &&
            (include_leaving || s_devices[i].state == SLOT_ACTIVE) &&
            s_devices[i].device.short_addr == short_addr) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static device_ref_t ref_for(const device_slot_t *slot)
{
    return (device_ref_t){
        .index = (uint8_t)(slot - s_devices),
        .generation = slot->generation,
    };
}

static device_slot_t *from_ref(device_ref_t ref, bool include_leaving)
{
    if (ref.index >= GATEWAY_MAX_DEVICES) {
        return NULL;
    }
    device_slot_t *slot = &s_devices[ref.index];
    if (slot->state == SLOT_EMPTY || slot->generation != ref.generation ||
        (!include_leaving && slot->state != SLOT_ACTIVE)) {
        return NULL;
    }
    return slot;
}

static void maybe_reclaim(device_slot_t *slot)
{
    if (slot != NULL && slot->state == SLOT_LEAVING &&
        slot->pending_jobs == 0U && slot->pending_requests == 0U) {
        memset(slot->endpoints, 0, sizeof(slot->endpoints));
        memset(&slot->device, 0, sizeof(slot->device));
        slot->device.short_addr = GATEWAY_INVALID_SHORT_ADDR;
        slot->state = SLOT_EMPTY;
    }
}

static device_slot_t *allocate_device(ezb_shortaddr_t short_addr)
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
            return &s_devices[i];
        }
    }
    return NULL;
}

/* IEEE is authoritative identity; the 16-bit address is a mutable route. */
static device_slot_t *upsert_device(
    ezb_shortaddr_t short_addr, const ezb_extaddr_t *ieee)
{
    device_slot_t *slot = ieee == NULL ?
        find_by_short(short_addr, false) : find_by_ieee(ieee, true);
    if (slot == NULL) {
        slot = allocate_device(short_addr);
    }
    if (slot == NULL) {
        return NULL;
    }

    device_slot_t *old = find_by_short(short_addr, true);
    if (old != NULL && old != slot) {
        old->device.short_addr = GATEWAY_INVALID_SHORT_ADDR;
        old->state = SLOT_LEAVING;
        maybe_reclaim(old);
    }

    slot->state = SLOT_ACTIVE;
    if (slot->device.short_addr != GATEWAY_INVALID_SHORT_ADDR &&
        slot->device.short_addr != short_addr) {
        slot->previous_short_addr = slot->device.short_addr;
    }
    slot->device.short_addr = short_addr;
    if (ieee != NULL) {
        memcpy(slot->device.ieee, ieee->u8, sizeof(slot->device.ieee));
        slot->device.ieee_valid = true;
    }
    return slot;
}

static endpoint_state_t *endpoint_state(
    device_slot_t *slot, uint8_t endpoint, bool create)
{
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
    warning(&slot->device, "endpoint state capacity exhausted");
    return NULL;
}

static gateway_device_id_t device_from_header(const ezb_zcl_cmd_hdr_t *header)
{
    gateway_device_id_t device = {.short_addr = GATEWAY_INVALID_SHORT_ADDR};
    if (header == NULL) {
        return device;
    }
    if (header->src_addr.addr_mode == EZB_ADDR_MODE_SHORT) {
        device.short_addr = header->src_addr.u.short_addr;
        device_slot_t *slot = find_by_short(device.short_addr, false);
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
        device_slot_t *slot = find_by_ieee(
            &header->src_addr.u.extended_addr, false
        );
        if (slot != NULL) {
            device = slot->device;
        }
    }
    return device;
}

static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries
);

static device_slot_t *recover_report_source(const ezb_zcl_cmd_hdr_t *header)
{
    if (header == NULL || header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT) {
        return NULL;
    }
    const ezb_shortaddr_t short_addr = header->src_addr.u.short_addr;
    device_slot_t *slot = find_by_short(short_addr, false);
    if (slot != NULL) {
        return slot;
    }

    ezb_extaddr_t ieee;
    slot = ezb_address_extended_by_short(short_addr, &ieee) == EZB_ERR_NONE ?
        upsert_device(short_addr, &ieee) : upsert_device(short_addr, NULL);
    if (slot != NULL &&
        !queue_job(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U, 0U, 0U)) {
        warning(&slot->device, "report recovery discovery queue full");
    }
    return slot;
}

static bool queue_job(
    discovery_kind_t kind,
    device_slot_t *slot,
    uint8_t endpoint,
    uint16_t cluster,
    uint8_t retries)
{
    if (s_discovery_queue == NULL || slot == NULL || slot->state != SLOT_ACTIVE ||
        slot->device.short_addr == GATEWAY_INVALID_SHORT_ADDR) {
        return false;
    }
    const discovery_job_t job = {
        .kind = kind,
        .device = ref_for(slot),
        .route_short_addr = slot->device.short_addr,
        .endpoint = endpoint,
        .cluster_id = cluster,
        .retry_count = retries,
    };
    if (xQueueSend(s_discovery_queue, &job, 0) != pdPASS) {
        return false;
    }
    ++slot->pending_jobs;
    return true;
}

static bool schedule_basic(device_slot_t *slot, uint8_t endpoint)
{
    endpoint_state_t *state = endpoint_state(slot, endpoint, true);
    if (state == NULL || state->basic_state != BASIC_NOT_SCHEDULED) {
        return false;
    }
    if (!queue_job(DISCOVERY_READ_BASIC, slot, endpoint, 0U, 0U)) {
        warning(&slot->device, "Basic read queue full");
        return false;
    }
    state->basic_state = BASIC_SCHEDULED;
    state->basic_scheduled_at_ms = gateway_uptime_ms();
    return true;
}

static bool schedule_binding(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    const uint8_t mask = gateway_reporting_policy_binding_mask(cluster);
    endpoint_state_t *state = endpoint_state(slot, endpoint, true);
    if (state == NULL || mask == 0U ||
        (state->binding_requested & mask) != 0U ||
        (state->binding_configured & mask) != 0U) {
        return false;
    }
    if (!queue_job(DISCOVERY_BIND_CLUSTER, slot, endpoint, cluster, 0U)) {
        warning(&slot->device, "binding queue full");
        return false;
    }
    state->binding_requested |= mask;
    state->binding_requested_at_ms = gateway_uptime_ms();
    return true;
}

static bool schedule_reporting(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster)
{
    const uint8_t mask = gateway_reporting_policy_reporting_mask(cluster);
    endpoint_state_t *state = endpoint_state(slot, endpoint, true);
    if (state == NULL || mask == 0U ||
        (state->binding_configured & mask) == 0U ||
        (state->reporting_requested & mask) != 0U ||
        (state->reporting_configured & mask) != 0U) {
        return false;
    }
    if (!queue_job(DISCOVERY_CONFIG_REPORTING, slot, endpoint, cluster, 0U)) {
        warning(&slot->device, "reporting queue full");
        return false;
    }
    state->reporting_requested |= mask;
    state->reporting_requested_at_ms = gateway_uptime_ms();
    return true;
}

static async_context_t *context_alloc(
    device_slot_t *slot, uint8_t endpoint, uint16_t cluster, uint8_t retries)
{
    for (size_t i = 0; i < GATEWAY_MAX_ASYNC_CONTEXTS; ++i) {
        if (!s_async_contexts[i].in_use) {
            s_async_contexts[i] = (async_context_t){
                .in_use = true,
                .device = ref_for(slot),
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
    device_slot_t *slot = from_ref(context->device, true);
    if (slot != NULL && slot->pending_requests != 0U) {
        --slot->pending_requests;
        maybe_reclaim(slot);
    }
    context->in_use = false;
}

static void clear_pending(device_slot_t *slot, const discovery_job_t *job)
{
    endpoint_state_t *state = endpoint_state(slot, job->endpoint, false);
    if (state == NULL) {
        return;
    }
    if (job->kind == DISCOVERY_READ_BASIC &&
        state->basic_state == BASIC_SCHEDULED) {
        state->basic_state = BASIC_NOT_SCHEDULED;
    }
    if (job->kind == DISCOVERY_BIND_CLUSTER) {
        state->binding_requested &= (uint8_t)~gateway_reporting_policy_binding_mask(job->cluster_id);
    }
    if (job->kind == DISCOVERY_CONFIG_REPORTING) {
        state->reporting_requested &= (uint8_t)~gateway_reporting_policy_reporting_mask(job->cluster_id);
    }
}

static void retry_or_fail(discovery_job_t job, const char *message)
{
    device_slot_t *slot = from_ref(job.device, true);
    if (slot == NULL) {
        return;
    }
    if (slot->state == SLOT_ACTIVE &&
        job.retry_count < GATEWAY_DISCOVERY_MAX_RETRIES &&
        queue_job(
            job.kind,
            slot,
            job.endpoint,
            job.cluster_id,
            (uint8_t)(job.retry_count + 1U))) {
        return;
    }
    clear_pending(slot, &job);
    warning(&slot->device, message);
    maybe_reclaim(slot);
}

static void publish_report(const ezb_zcl_cmd_report_attr_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT) {
        return;
    }
    device_slot_t *slot = recover_report_source(header);
    gateway_device_id_t device =
        slot == NULL ? device_from_header(header) : slot->device;

    for (ezb_zcl_report_attr_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        gateway_measurement_kind_t kind;
        gateway_unit_t unit;
        double value;
        if (gateway_zcl_normalize(
                header->cluster_id,
                item->attr_id,
                item->attr_type,
                item->attr_value,
                &kind,
                &unit,
                &value)) {
            gateway_event_t event = event_base(
                GATEWAY_EVENT_MEASUREMENT, &device
            );
            event.endpoint = header->src_ep;
            event.data.measurement = (typeof(event.data.measurement)){
                .kind = kind,
                .unit = unit,
                .value = value,
                .cluster_id = header->cluster_id,
                .attribute_id = item->attr_id,
                .zcl_type = item->attr_type,
            };
            gateway_event_publish(&event);
        } else {
            gateway_event_t event = event_base(
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
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT ||
        header->cluster_id != EZB_ZCL_CLUSTER_ID_BASIC) {
        return;
    }

    gateway_device_id_t device = device_from_header(header);
    bool seen = false;
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
        gateway_event_t event = event_base(GATEWAY_EVENT_BASIC, &device);
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
        gateway_event_publish(&event);
    }

    device_slot_t *slot = find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : endpoint_state(slot, header->src_ep, false);
    if (seen && state != NULL) {
        state->basic_state = BASIC_COMPLETE;
    }
}

static void publish_config_response(
    const ezb_zcl_cmd_config_report_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT) {
        return;
    }
    gateway_device_id_t device = device_from_header(header);
    device_slot_t *slot = find_by_short(device.short_addr, false);
    endpoint_state_t *state =
        slot == NULL ? NULL : endpoint_state(slot, header->src_ep, false);
    const uint8_t mask = gateway_reporting_policy_reporting_mask(header->cluster_id);

    for (ezb_zcl_config_report_rsp_variable_t *item = message->in.variables;
         item != NULL;
         item = item->next) {
        if (state != NULL && (state->reporting_requested & mask) != 0U) {
            state->reporting_requested &= (uint8_t)~mask;
            if (item->status == EZB_ZCL_STATUS_SUCCESS) {
                state->reporting_configured |= mask;
            }
        }
        gateway_event_t event = event_base(
            GATEWAY_EVENT_REPORTING_CONFIG, &device
        );
        event.endpoint = header->src_ep;
        event.data.reporting.cluster_id = header->cluster_id;
        event.data.reporting.attribute_id = item->attr_id;
        event.data.reporting.status = item->status;
        gateway_event_publish(&event);
    }
}

static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)
{
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    message->out.start_fast_poll = true;
    message->out.fast_poll_timeout = GATEWAY_FAST_POLL_TIMEOUT_QUARTER_SECONDS;

    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT ||
        header->src_addr.addr_mode != EZB_ADDR_MODE_SHORT) {
        return;
    }
    device_slot_t *slot = upsert_device(header->src_addr.u.short_addr, NULL);
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
                GATEWAY_REQUEST_STALE_MS) {
            state->basic_state = BASIC_NOT_SCHEDULED;
        }
        if (state->binding_requested != 0U &&
            (uint32_t)(now_ms - state->binding_requested_at_ms) >=
                GATEWAY_REQUEST_STALE_MS) {
            state->binding_requested = 0U;
        }
        if (state->reporting_requested != 0U &&
            (uint32_t)(now_ms - state->reporting_requested_at_ms) >=
                GATEWAY_REQUEST_STALE_MS) {
            state->reporting_requested = 0U;
        }
    }

    gateway_event_t event = event_base(
        GATEWAY_EVENT_DEVICE_CHECK_IN, &slot->device
    );
    event.endpoint = header->src_ep;
    gateway_event_publish(&event);
    if (!queue_job(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U, 0U, 0U)) {
        warning(&slot->device, "check-in discovery queue full");
    }
}

static void zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id == EZB_ZCL_CORE_REPORT_ATTR_CB_ID) {
        publish_report(message);
    } else if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {
        publish_basic_read(message);
    } else if (callback_id == EZB_ZCL_CORE_CONFIG_REPORT_RSP_CB_ID) {
        publish_config_response(message);
    } else if (callback_id == EZB_ZCL_CORE_POLL_CONTROL_CHECK_IN_CB_ID) {
        handle_check_in(message);
    }
}

static bool context_route(const async_context_t *context, device_slot_t **out)
{
    device_slot_t *slot = context == NULL ? NULL : from_ref(context->device, false);
    if (slot == NULL || slot->device.short_addr != context->route_short_addr) {
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
        endpoint_state_t *state = endpoint_state(
            slot, context->endpoint, false
        );
        if (status == EZB_ZDP_STATUS_SUCCESS && state != NULL) {
            const uint8_t mask = gateway_reporting_policy_binding_mask(context->cluster_id);
            state->binding_requested &= (uint8_t)~mask;
            state->binding_configured |= mask;
            schedule_reporting(slot, context->endpoint, context->cluster_id);
        } else if (state != NULL) {
            state->binding_requested |= gateway_reporting_policy_binding_mask(context->cluster_id);
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
        gateway_event_t event = event_base(
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
    gateway_event_t event = event_base(GATEWAY_EVENT_ENDPOINT, &slot->device);
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

    bool basic = false;
    for (uint8_t i = 0; i < desc->app_input_cluster_count; ++i) {
        const uint16_t cluster = desc->app_cluster_list[i];
        if (cluster == EZB_ZCL_CLUSTER_ID_BASIC) {
            basic = true;
        }
        if (gateway_reporting_policy_binding_mask(cluster) != 0U) {
            schedule_binding(slot, desc->ep_id, cluster);
        } else if (gateway_reporting_policy_reporting_mask(cluster) != 0U) {
            schedule_reporting(slot, desc->ep_id, cluster);
        }
    }
    if (basic) {
        schedule_basic(slot, desc->ep_id);
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
            warning(&slot->device, "simple descriptor queue full");
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
            .src_ep = GATEWAY_ENDPOINT,
            .cluster_id = EZB_ZCL_CLUSTER_ID_BASIC,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.attr_number = 2U, .attr_field = attrs},
    };
    return ezb_zcl_read_attr_cmd_req(&request) == EZB_ERR_NONE;
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
    request.field.dst_ep = GATEWAY_ENDPOINT;
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
    gateway_reporting_spec_t spec;
    if (!gateway_reporting_policy_spec(job->cluster_id, &spec)) {
        return true;
    }

    ezb_zcl_config_report_record_t record = {
        .direction = EZB_ZCL_REPORTING_SEND,
        .attr_id = spec.attribute_id,
    };
    record.client.attr_type = spec.attribute_type;
    record.client.min_interval = spec.min_interval;
    record.client.max_interval = spec.max_interval;
    switch (spec.change_kind) {
    case GATEWAY_REPORTING_CHANGE_S16:
        record.client.reportable_change.s16 = (int16_t)spec.reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U16:
        record.client.reportable_change.u16 = (uint16_t)spec.reportable_change;
        break;
    case GATEWAY_REPORTING_CHANGE_U8:
        record.client.reportable_change.u8 = (uint8_t)spec.reportable_change;
        break;
    default:
        return false;
    }

    const ezb_zcl_config_report_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr),
            .dst_ep = job->endpoint,
            .src_ep = GATEWAY_ENDPOINT,
            .cluster_id = job->cluster_id,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.record_number = 1U, .record_field = &record},
    };
    return ezb_zcl_config_report_cmd_req(&request) == EZB_ERR_NONE;
}

static void discovery_task(void *arg)
{
    (void)arg;
    discovery_job_t job;
    for (;;) {
        if (xQueueReceive(s_discovery_queue, &job, portMAX_DELAY) != pdPASS) {
            continue;
        }
        device_slot_t *slot = from_ref(job.device, true);
        if (slot == NULL) {
            continue;
        }
        if (slot->pending_jobs != 0U) {
            --slot->pending_jobs;
        }
        if (slot->state != SLOT_ACTIVE ||
            slot->device.short_addr != job.route_short_addr) {
            clear_pending(slot, &job);
            warning(&slot->device, "discovery cancelled: route superseded");
            maybe_reclaim(slot);
            continue;
        }
        if (job.retry_count != 0U) {
            vTaskDelay(pdMS_TO_TICKS(
                GATEWAY_DISCOVERY_RETRY_DELAY_MS * job.retry_count
            ));
        }
        if (!esp_zigbee_lock_acquire(
                pdMS_TO_TICKS(GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS))) {
            retry_or_fail(job, "discovery lock timeout after retries");
            continue;
        }
        const bool sent =
            job.kind == DISCOVERY_ACTIVE_ENDPOINTS ? submit_active(slot, &job) :
            job.kind == DISCOVERY_SIMPLE_DESCRIPTOR ? submit_simple(slot, &job) :
            job.kind == DISCOVERY_READ_BASIC ? submit_basic(slot, &job) :
            job.kind == DISCOVERY_BIND_CLUSTER ? submit_binding(slot, &job) :
            submit_reporting(slot, &job);
        esp_zigbee_lock_release();
        if (!sent) {
            retry_or_fail(job, "discovery submit failed after retries");
        }
    }
}

static void network_event(gateway_event_kind_t kind)
{
    gateway_event_t event = event_base(kind, NULL);
    gateway_event_publish(&event);
}

static void announce_and_discover(
    gateway_event_kind_t kind,
    ezb_shortaddr_t short_addr,
    const ezb_extaddr_t *ieee)
{
    device_slot_t *existing =
        ieee == NULL ? NULL : find_by_ieee(ieee, true);
    const ezb_shortaddr_t old_short_addr = existing == NULL ?
        GATEWAY_INVALID_SHORT_ADDR :
        (existing->device.short_addr == GATEWAY_INVALID_SHORT_ADDR ?
            existing->previous_short_addr : existing->device.short_addr);

    device_slot_t *slot = upsert_device(short_addr, ieee);
    if (slot == NULL) {
        warning(NULL, "device registry capacity exhausted");
        return;
    }

    gateway_event_t event = event_base(kind, &slot->device);
    if (kind == GATEWAY_EVENT_DEVICE_REJOIN) {
        event.data.rejoin.old_short_addr = old_short_addr;
        event.data.rejoin.new_short_addr = slot->device.short_addr;
    }
    gateway_event_publish(&event);
    if (!queue_job(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U, 0U, 0U)) {
        warning(&slot->device, "active endpoint queue full");
    }
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

static void device_left(
    ezb_shortaddr_t short_addr,
    const ezb_extaddr_t *ieee,
    bool leave_type_known,
    ezb_zdo_leave_type_t leave_type)
{
    device_slot_t *slot = ieee == NULL ? NULL : find_by_ieee(ieee, true);
    if (slot == NULL) {
        slot = find_by_short(short_addr, true);
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

    gateway_event_t event = event_base(kind, &device);
    event.data.leave.leave_type = leave_type_known ? leave_type : UINT8_MAX;
    event.data.leave.record_retained = retained;
    gateway_event_publish(&event);

    if (slot == NULL) {
        return;
    }
    if (kind == GATEWAY_EVENT_DEVICE_LEAVE_RESET) {
        slot->state = SLOT_LEAVING;
        maybe_reclaim(slot);
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
        gateway_event_t event = event_base(GATEWAY_EVENT_PERMIT_JOIN, NULL);
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
            announce_and_discover(
                GATEWAY_EVENT_DEVICE_UPDATE, p->short_addr, &p->device_addr
            );
        }
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
            device_slot_t *slot = find_by_ieee(&p->device_addr, false);
            if (slot == NULL) {
                slot = find_by_short(p->short_addr, false);
            }
            if (slot != NULL) {
                device = slot->device;
            }
            gateway_event_t event = event_base(
                GATEWAY_EVENT_DEVICE_UNAVAILABLE, &device
            );
            gateway_event_publish(&event);
        }
    }
    return false;
}

static void zigbee_task(void *arg)
{
    (void)arg;
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
        return;
    }

    s_discovery_queue = xQueueCreateStatic(
        GATEWAY_DISCOVERY_QUEUE_DEPTH,
        sizeof(discovery_job_t),
        s_discovery_queue_buffer,
        &s_discovery_queue_storage
    );
    ezb_bdb_set_primary_channel_set(GATEWAY_CHANNEL_MASK);

    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = GATEWAY_ENDPOINT,
        .app_profile_id = GATEWAY_PROFILE_ID,
        .app_device_id = GATEWAY_DEVICE_ID,
        .app_device_version = 1U,
    };
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t endpoint = ezb_af_create_gateway_endpoint(&endpoint_config);
    if (device == EZB_INVALID_AF_DEVICE_DESC ||
        endpoint == EZB_INVALID_AF_EP_DESC ||
        ezb_af_device_add_endpoint_desc(device, endpoint) != EZB_ERR_NONE ||
        ezb_af_device_desc_register(device) != EZB_ERR_NONE) {
        return;
    }

    ezb_app_signal_add_handler(app_signal_handler);
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    ezb_secur_tcpol_set_allow_rejoins_with_well_known_key(true);
    if (esp_zigbee_start(false) != ESP_OK) {
        return;
    }
    network_event(GATEWAY_EVENT_STACK_READY);
    xTaskCreate(discovery_task, "zb_discovery", 4096, NULL, 5, NULL);
    esp_zigbee_launch_mainloop();
}

void zigbee_gateway_start(void)
{
    xTaskCreate(zigbee_task, "zigbee_main", 6144, NULL, 5, NULL);
}

void zigbee_gateway_set_permit_join(uint8_t seconds)
{
    if (!esp_zigbee_lock_acquire(
            pdMS_TO_TICKS(GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS))) {
        warning(NULL, "permit command rejected: Zigbee lock timeout");
        return;
    }
    if (seconds == 0U) {
        ezb_bdb_close_network();
    } else {
        ezb_bdb_open_network(seconds);
    }
    esp_zigbee_lock_release();
}
