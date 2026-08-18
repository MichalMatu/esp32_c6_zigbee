#include "zigbee_gateway.h"

#include <string.h>

#include "esp_err.h"
#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <ezbee/af.h>
#include <ezbee/app_signals.h>
#include <ezbee/bdb.h>
#include <ezbee/zcl/zcl_core.h>
#include <ezbee/zcl/zcl_general_cmd.h>
#include <ezbee/zdo/zdo_dev_srv_disc.h>

#include "gateway_events.h"

#define GATEWAY_ENDPOINT 1U
#define GATEWAY_PROFILE_ID 0x0104U
#define GATEWAY_DEVICE_ID 0x0000U
#define GATEWAY_CHANNEL_MASK 0x07fff800UL
#define GATEWAY_MAX_DEVICES 16U
#define GATEWAY_DISCOVERY_QUEUE_DEPTH 16U
#define GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS 100U

#define ZCL_ATTR_BASIC_MANUFACTURER_NAME 0x0004U
#define ZCL_ATTR_BASIC_MODEL_IDENTIFIER 0x0005U
#define ZCL_ATTR_MEASURED_VALUE 0x0000U
#define ZCL_ATTR_OCCUPANCY 0x0000U
#define ZCL_ATTR_ON_OFF 0x0000U
#define ZCL_ATTR_BATTERY_VOLTAGE 0x0020U
#define ZCL_ATTR_BATTERY_PERCENT 0x0021U
#define ZCL_ATTR_MAINS_VOLTAGE 0x0000U
#define ZCL_ATTR_RMS_VOLTAGE 0x0505U
#define ZCL_ATTR_RMS_CURRENT 0x0508U
#define ZCL_ATTR_ACTIVE_POWER 0x050bU
#define ZCL_ATTR_CURRENT_SUMMATION_DELIVERED 0x0000U

typedef struct {
    bool in_use;
    gateway_device_id_t device;
    bool basic_read_started;
} gateway_device_slot_t;

typedef enum {
    DISCOVERY_ACTIVE_ENDPOINTS,
    DISCOVERY_SIMPLE_DESCRIPTOR,
    DISCOVERY_READ_BASIC,
} discovery_kind_t;

typedef struct {
    discovery_kind_t kind;
    gateway_device_slot_t *slot;
    uint8_t endpoint;
} discovery_job_t;

static gateway_device_slot_t s_devices[GATEWAY_MAX_DEVICES];
static StaticQueue_t s_discovery_queue_storage;
static uint8_t s_discovery_queue_buffer[GATEWAY_DISCOVERY_QUEUE_DEPTH * sizeof(discovery_job_t)];
static QueueHandle_t s_discovery_queue;

static gateway_event_t gateway_event_base(gateway_event_kind_t kind, const gateway_device_id_t *device)
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

static gateway_device_slot_t *find_device_by_short(ezb_shortaddr_t short_addr)
{
    for (size_t i = 0; i < GATEWAY_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && s_devices[i].device.short_addr == short_addr) {
            return &s_devices[i];
        }
    }
    return NULL;
}

static gateway_device_slot_t *upsert_device(ezb_shortaddr_t short_addr, const ezb_extaddr_t *ieee)
{
    gateway_device_slot_t *slot = find_device_by_short(short_addr);
    if (slot == NULL) {
        for (size_t i = 0; i < GATEWAY_MAX_DEVICES; ++i) {
            if (!s_devices[i].in_use) {
                slot = &s_devices[i];
                memset(slot, 0, sizeof(*slot));
                slot->in_use = true;
                slot->device.short_addr = short_addr;
                break;
            }
        }
    }
    if (slot != NULL && ieee != NULL) {
        if (!slot->device.ieee_valid || memcmp(slot->device.ieee, ieee->u8, sizeof(slot->device.ieee)) != 0) {
            slot->basic_read_started = false;
        }
        memcpy(slot->device.ieee, ieee->u8, sizeof(slot->device.ieee));
        slot->device.ieee_valid = true;
    }
    return slot;
}

static gateway_device_id_t device_from_header(const ezb_zcl_cmd_hdr_t *header)
{
    gateway_device_id_t device = {.short_addr = 0xffffU};
    if (header != NULL && header->src_addr.addr_mode == EZB_ADDR_MODE_SHORT) {
        gateway_device_slot_t *slot = find_device_by_short(header->src_addr.u.short_addr);
        device.short_addr = header->src_addr.u.short_addr;
        if (slot != NULL) {
            device = slot->device;
        }
    } else if (header != NULL && header->src_addr.addr_mode == EZB_ADDR_MODE_EXT) {
        memcpy(device.ieee, header->src_addr.u.extended_addr.u8, sizeof(device.ieee));
        device.ieee_valid = true;
    }
    return device;
}

static bool queue_discovery(discovery_kind_t kind, gateway_device_slot_t *slot, uint8_t endpoint)
{
    const discovery_job_t job = {.kind = kind, .slot = slot, .endpoint = endpoint};
    return s_discovery_queue != NULL && xQueueSend(s_discovery_queue, &job, 0) == pdPASS;
}

static uint16_t attr_size(uint8_t type, const void *value)
{
    return value == NULL ? 0U : ezb_zcl_get_attr_value_size((ezb_zcl_attr_type_t)type, value);
}

static bool read_u8(const void *value, uint8_t type, uint8_t *out)
{
    if (value == NULL || (type != EZB_ZCL_ATTR_TYPE_UINT8 && type != EZB_ZCL_ATTR_TYPE_BOOL && type != EZB_ZCL_ATTR_TYPE_MAP8)) return false;
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_u16(const void *value, uint8_t type, uint16_t *out)
{
    if (value == NULL || type != EZB_ZCL_ATTR_TYPE_UINT16) return false;
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_s16(const void *value, uint8_t type, int16_t *out)
{
    if (value == NULL || type != EZB_ZCL_ATTR_TYPE_INT16) return false;
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_float(const void *value, uint8_t type, float *out)
{
    if (value == NULL || type != EZB_ZCL_ATTR_TYPE_SINGLE) return false;
    memcpy(out, value, sizeof(*out));
    return true;
}

static bool read_u48(const void *value, uint8_t type, uint64_t *out)
{
    if (value == NULL || type != EZB_ZCL_ATTR_TYPE_UINT48) return false;
    const uint8_t *bytes = value;
    *out = 0U;
    for (unsigned i = 0; i < 6U; ++i) *out |= (uint64_t)bytes[i] << (8U * i);
    return true;
}

static bool normalize_measurement(uint16_t cluster, uint16_t attr, uint8_t type, const void *value,
                                  gateway_measurement_kind_t *kind, gateway_unit_t *unit, double *number)
{
    uint8_t u8;
    uint16_t u16;
    int16_t s16;
    uint64_t u48;
    float single;
    if (cluster == EZB_ZCL_CLUSTER_ID_TEMPERATURE_MEASUREMENT && attr == ZCL_ATTR_MEASURED_VALUE && read_s16(value, type, &s16)) {
        *kind = GATEWAY_MEAS_TEMPERATURE; *unit = GATEWAY_UNIT_CELSIUS; *number = (double)s16 / 100.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT && attr == ZCL_ATTR_MEASURED_VALUE && read_u16(value, type, &u16)) {
        *kind = GATEWAY_MEAS_HUMIDITY; *unit = GATEWAY_UNIT_PERCENT; *number = (double)u16 / 100.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT && attr == ZCL_ATTR_MEASURED_VALUE && read_u16(value, type, &u16)) {
        *kind = GATEWAY_MEAS_ILLUMINANCE; *unit = GATEWAY_UNIT_LUX_LOG; *number = u16; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING && attr == ZCL_ATTR_OCCUPANCY && read_u8(value, type, &u8)) {
        *kind = GATEWAY_MEAS_OCCUPANCY; *unit = GATEWAY_UNIT_BOOLEAN; *number = (u8 & 1U) != 0U; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT && attr == ZCL_ATTR_MEASURED_VALUE && read_float(value, type, &single)) {
        *kind = GATEWAY_MEAS_CO2; *unit = GATEWAY_UNIT_PPM; *number = (double)single * 1000000.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG && attr == ZCL_ATTR_BATTERY_VOLTAGE && read_u8(value, type, &u8)) {
        *kind = GATEWAY_MEAS_BATTERY_VOLTAGE; *unit = GATEWAY_UNIT_VOLTS; *number = (double)u8 / 10.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG && attr == ZCL_ATTR_BATTERY_PERCENT && read_u8(value, type, &u8)) {
        *kind = GATEWAY_MEAS_BATTERY_PERCENT; *unit = GATEWAY_UNIT_PERCENT; *number = (double)u8 / 2.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_POWER_CONFIG && attr == ZCL_ATTR_MAINS_VOLTAGE && read_u16(value, type, &u16)) {
        *kind = GATEWAY_MEAS_MAINS_VOLTAGE; *unit = GATEWAY_UNIT_VOLTS; *number = (double)u16 / 10.0; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT && attr == ZCL_ATTR_RMS_VOLTAGE && read_u16(value, type, &u16)) {
        *kind = GATEWAY_MEAS_VOLTAGE; *unit = GATEWAY_UNIT_VOLTS; *number = u16; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT && attr == ZCL_ATTR_RMS_CURRENT && read_u16(value, type, &u16)) {
        *kind = GATEWAY_MEAS_CURRENT; *unit = GATEWAY_UNIT_AMPS; *number = u16; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT && attr == ZCL_ATTR_ACTIVE_POWER && read_s16(value, type, &s16)) {
        *kind = GATEWAY_MEAS_POWER; *unit = GATEWAY_UNIT_WATTS; *number = s16; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_METERING && attr == ZCL_ATTR_CURRENT_SUMMATION_DELIVERED && read_u48(value, type, &u48)) {
        *kind = GATEWAY_MEAS_ENERGY; *unit = GATEWAY_UNIT_KILOWATT_HOURS; *number = (double)u48; return true;
    }
    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attr == ZCL_ATTR_ON_OFF && read_u8(value, type, &u8)) {
        *kind = GATEWAY_MEAS_ON_OFF; *unit = GATEWAY_UNIT_BOOLEAN; *number = u8 != 0U; return true;
    }
    return false;
}

static void publish_report(const ezb_zcl_cmd_report_attr_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT) return;
    const gateway_device_id_t device = device_from_header(header);
    for (ezb_zcl_report_attr_variable_t *item = message->in.variables; item != NULL; item = item->next) {
        gateway_measurement_kind_t kind;
        gateway_unit_t unit;
        double value;
        if (normalize_measurement(header->cluster_id, item->attr_id, item->attr_type, item->attr_value, &kind, &unit, &value)) {
            gateway_event_t event = gateway_event_base(GATEWAY_EVENT_MEASUREMENT, &device);
            event.endpoint = header->src_ep;
            event.data.measurement = (typeof(event.data.measurement)){
                .kind = kind, .unit = unit, .value = value, .cluster_id = header->cluster_id,
                .attribute_id = item->attr_id, .zcl_type = item->attr_type,
            };
            gateway_event_publish(&event);
            continue;
        }
        gateway_event_t event = gateway_event_base(GATEWAY_EVENT_RAW_ATTRIBUTE, &device);
        event.endpoint = header->src_ep;
        event.data.raw.cluster_id = header->cluster_id;
        event.data.raw.attribute_id = item->attr_id;
        event.data.raw.zcl_type = item->attr_type;
        event.data.raw.original_length = attr_size(item->attr_type, item->attr_value);
        event.data.raw.copied_length = event.data.raw.original_length > GATEWAY_RAW_ATTRIBUTE_MAX_BYTES ? GATEWAY_RAW_ATTRIBUTE_MAX_BYTES : event.data.raw.original_length;
        event.data.raw.truncated = event.data.raw.original_length > GATEWAY_RAW_ATTRIBUTE_MAX_BYTES;
        if (event.data.raw.copied_length != 0U) memcpy(event.data.raw.bytes, item->attr_value, event.data.raw.copied_length);
        gateway_event_publish(&event);
    }
}

static void publish_basic_read(const ezb_zcl_cmd_read_attr_rsp_message_t *message)
{
    const ezb_zcl_cmd_hdr_t *header = message->in.header;
    if (header == NULL || message->info.dst_ep != GATEWAY_ENDPOINT || header->cluster_id != EZB_ZCL_CLUSTER_ID_BASIC) return;
    const gateway_device_id_t device = device_from_header(header);
    for (ezb_zcl_read_attr_rsp_variable_t *item = message->in.variables; item != NULL; item = item->next) {
        if (item->status != EZB_ZCL_STATUS_SUCCESS || item->attr_type != EZB_ZCL_ATTR_TYPE_STRING || item->attr_value == NULL ||
            (item->attr_id != ZCL_ATTR_BASIC_MANUFACTURER_NAME && item->attr_id != ZCL_ATTR_BASIC_MODEL_IDENTIFIER)) continue;
        const uint8_t *zcl_string = item->attr_value;
        if (zcl_string[0] == 0xffU) continue;
        gateway_event_t event = gateway_event_base(GATEWAY_EVENT_BASIC, &device);
        event.endpoint = header->src_ep;
        strncpy(event.data.text.key, item->attr_id == ZCL_ATTR_BASIC_MANUFACTURER_NAME ? "manufacturer" : "model", sizeof(event.data.text.key) - 1U);
        size_t length = zcl_string[0] < (GATEWAY_TEXT_MAX_BYTES - 1U) ? zcl_string[0] : (GATEWAY_TEXT_MAX_BYTES - 1U);
        memcpy(event.data.text.value, zcl_string + 1, length);
        event.data.text.value[length] = '\0';
        gateway_event_publish(&event);
    }
}

static void zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id == EZB_ZCL_CORE_REPORT_ATTR_CB_ID) {
        publish_report((const ezb_zcl_cmd_report_attr_message_t *)message);
    } else if (callback_id == EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID) {
        publish_basic_read((const ezb_zcl_cmd_read_attr_rsp_message_t *)message);
    }
}

static void simple_desc_callback(const ezb_zdo_simple_desc_req_result_t *result, void *user_ctx)
{
    gateway_device_slot_t *slot = user_ctx;
    if (slot == NULL || result == NULL || result->error != EZB_ERR_NONE || result->rsp == NULL || result->rsp->status != EZB_ZDP_STATUS_SUCCESS) return;
    const ezb_af_simple_desc_t *desc = &result->rsp->desc;
    gateway_event_t event = gateway_event_base(GATEWAY_EVENT_ENDPOINT, &slot->device);
    event.endpoint = desc->ep_id;
    event.data.endpoint_desc.profile_id = desc->app_profile_id;
    event.data.endpoint_desc.device_id = desc->app_device_id;
    event.data.endpoint_desc.input_count = desc->app_input_cluster_count;
    event.data.endpoint_desc.output_count = desc->app_output_cluster_count;
    event.data.endpoint_desc.input_copied = desc->app_input_cluster_count > GATEWAY_MAX_DESCRIPTOR_CLUSTERS ? GATEWAY_MAX_DESCRIPTOR_CLUSTERS : desc->app_input_cluster_count;
    event.data.endpoint_desc.output_copied = desc->app_output_cluster_count > GATEWAY_MAX_DESCRIPTOR_CLUSTERS ? GATEWAY_MAX_DESCRIPTOR_CLUSTERS : desc->app_output_cluster_count;
    for (uint8_t i = 0; i < event.data.endpoint_desc.input_copied; ++i) event.data.endpoint_desc.input_clusters[i] = desc->app_cluster_list[i];
    for (uint8_t i = 0; i < event.data.endpoint_desc.output_copied; ++i) event.data.endpoint_desc.output_clusters[i] = desc->app_cluster_list[desc->app_input_cluster_count + i];
    gateway_event_publish(&event);
    for (uint8_t i = 0; i < desc->app_input_cluster_count; ++i) {
        if (desc->app_cluster_list[i] == EZB_ZCL_CLUSTER_ID_BASIC && !slot->basic_read_started) {
            slot->basic_read_started = true;
            queue_discovery(DISCOVERY_READ_BASIC, slot, desc->ep_id);
            break;
        }
    }
}

static void active_ep_callback(const ezb_zdo_active_ep_req_result_t *result, void *user_ctx)
{
    gateway_device_slot_t *slot = user_ctx;
    if (slot == NULL || result == NULL || result->error != EZB_ERR_NONE || result->rsp == NULL || result->rsp->status != EZB_ZDP_STATUS_SUCCESS) return;
    for (uint8_t i = 0; i < result->rsp->active_ep_count; ++i) {
        queue_discovery(DISCOVERY_SIMPLE_DESCRIPTOR, slot, result->rsp->active_ep_list[i]);
    }
}

static void submit_active_endpoints(gateway_device_slot_t *slot)
{
    ezb_zdo_active_ep_req_t request = {
        .dst_nwk_addr = slot->device.short_addr,
        .field = {.nwk_addr_of_interest = slot->device.short_addr},
        .cb = active_ep_callback,
        .user_ctx = slot,
    };
    ezb_zdo_active_ep_req(&request);
}

static void submit_simple_descriptor(gateway_device_slot_t *slot, uint8_t endpoint)
{
    ezb_zdo_simple_desc_req_t request = {
        .dst_nwk_addr = slot->device.short_addr,
        .field = {.nwk_addr_of_interest = slot->device.short_addr, .endpoint = endpoint},
        .cb = simple_desc_callback,
        .user_ctx = slot,
    };
    ezb_zdo_simple_desc_req(&request);
}

static void submit_basic_read(gateway_device_slot_t *slot, uint8_t endpoint)
{
    uint16_t attrs[] = {ZCL_ATTR_BASIC_MANUFACTURER_NAME, ZCL_ATTR_BASIC_MODEL_IDENTIFIER};
    ezb_zcl_read_attr_cmd_t request = {
        .cmd_ctrl = {
            .dst_addr = EZB_ADDRESS_SHORT(slot->device.short_addr), .dst_ep = endpoint, .src_ep = GATEWAY_ENDPOINT,
            .cluster_id = EZB_ZCL_CLUSTER_ID_BASIC, .manuf_code = EZB_ZCL_STD_MANUF_CODE,
        },
        .payload = {.attr_number = 2U, .attr_field = attrs},
    };
    ezb_zcl_read_attr_cmd_req(&request);
}

static void discovery_task(void *arg)
{
    discovery_job_t job;
    for (;;) {
        if (xQueueReceive(s_discovery_queue, &job, portMAX_DELAY) != pdPASS || job.slot == NULL) continue;
        if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS))) {
            gateway_event_t event = gateway_event_base(GATEWAY_EVENT_WARNING, &job.slot->device);
            strncpy(event.data.text.value, "discovery deferred: Zigbee lock timeout", sizeof(event.data.text.value) - 1U);
            gateway_event_publish(&event);
            continue;
        }
        if (job.kind == DISCOVERY_ACTIVE_ENDPOINTS) submit_active_endpoints(job.slot);
        else if (job.kind == DISCOVERY_SIMPLE_DESCRIPTOR) submit_simple_descriptor(job.slot, job.endpoint);
        else submit_basic_read(job.slot, job.endpoint);
        esp_zigbee_lock_release();
    }
}

static void publish_network_event(gateway_event_kind_t kind)
{
    gateway_event_t event = gateway_event_base(kind, NULL);
    gateway_event_publish(&event);
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
        publish_network_event(GATEWAY_EVENT_NETWORK_RESTORED);
    } else if (type == EZB_BDB_SIGNAL_FORMATION) {
        const ezb_bdb_signal_simple_params_t *formation = params;
        if (formation != NULL && formation->status == EZB_BDB_STATUS_SUCCESS) {
            publish_network_event(GATEWAY_EVENT_NETWORK_FORMED);
            ezb_bdb_open_network(180U);
        }
    } else if (type == EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS) {
        const ezb_nwk_signal_permit_join_status_params_t *permit = params;
        gateway_event_t event = gateway_event_base(GATEWAY_EVENT_PERMIT_JOIN, NULL);
        event.data.permit.duration = permit == NULL ? 0U : permit->duration;
        gateway_event_publish(&event);
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_ANNCE) {
        const ezb_zdo_signal_device_annce_params_t *announcement = params;
        if (announcement != NULL) {
            gateway_device_slot_t *slot = upsert_device(announcement->short_addr, &announcement->device_addr);
            if (slot != NULL) {
                gateway_event_t event = gateway_event_base(GATEWAY_EVENT_DEVICE_ANNOUNCE, &slot->device);
                gateway_event_publish(&event);
                queue_discovery(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U);
            }
        }
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_UPDATE) {
        const ezb_zdo_signal_device_update_params_t *update = params;
        if (update != NULL) {
            gateway_device_slot_t *slot = upsert_device(update->short_addr, &update->device_addr);
            if (slot != NULL) {
                gateway_event_t event = gateway_event_base(update->status == EZB_ZDO_UPDDEV_SECURE_REJOIN || update->status == EZB_ZDO_UPDDEV_TC_REJOIN ? GATEWAY_EVENT_DEVICE_REJOIN : GATEWAY_EVENT_DEVICE_UPDATE, &slot->device);
                gateway_event_publish(&event);
                if (event.kind == GATEWAY_EVENT_DEVICE_REJOIN) queue_discovery(DISCOVERY_ACTIVE_ENDPOINTS, slot, 0U);
            }
        }
    } else if (type == EZB_ZDO_SIGNAL_LEAVE_INDICATION) {
        const ezb_zdo_signal_leave_indication_params_t *leave = params;
        if (leave != NULL) {
            gateway_device_slot_t *slot = upsert_device(leave->short_addr, &leave->device_addr);
            if (slot != NULL) {
                gateway_event_t event = gateway_event_base(GATEWAY_EVENT_DEVICE_LEAVE, &slot->device);
                gateway_event_publish(&event);
            }
        }
    } else if (type == EZB_ZDO_SIGNAL_DEVICE_UNAVAILABLE) {
        const ezb_zdo_signal_device_unavailable_params_t *unavailable = params;
        if (unavailable != NULL) {
            gateway_device_slot_t *slot = upsert_device(unavailable->short_addr, &unavailable->device_addr);
            if (slot != NULL) {
                gateway_event_t event = gateway_event_base(GATEWAY_EVENT_DEVICE_UNAVAILABLE, &slot->device);
                gateway_event_publish(&event);
            }
        }
    }
    return false;
}

static void zigbee_task(void *arg)
{
    const esp_zigbee_config_t config = {
        .device_config = {.device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR, .install_code_policy = false, .zczr_config = {.max_children = 32U}},
        .platform_config = {.storage_partition_name = "zb_storage", .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE}},
    };
    if (esp_zigbee_init(&config) != ESP_OK) return;
    s_discovery_queue = xQueueCreateStatic(GATEWAY_DISCOVERY_QUEUE_DEPTH, sizeof(discovery_job_t), s_discovery_queue_buffer, &s_discovery_queue_storage);
    ezb_bdb_set_primary_channel_set(GATEWAY_CHANNEL_MASK);
    const ezb_af_ep_config_t endpoint_config = {.ep_id = GATEWAY_ENDPOINT, .app_profile_id = GATEWAY_PROFILE_ID, .app_device_id = GATEWAY_DEVICE_ID, .app_device_version = 1U};
    ezb_af_device_desc_t device_desc = ezb_af_create_device_desc();
    ezb_af_ep_desc_t endpoint = ezb_af_create_gateway_endpoint(&endpoint_config);
    if (device_desc == EZB_INVALID_AF_DEVICE_DESC || endpoint == EZB_INVALID_AF_EP_DESC ||
        ezb_af_device_add_endpoint_desc(device_desc, endpoint) != EZB_ERR_NONE || ezb_af_device_desc_register(device_desc) != EZB_ERR_NONE) return;
    ezb_app_signal_add_handler(app_signal_handler);
    ezb_zcl_core_action_handler_register(zcl_core_action_handler);
    if (esp_zigbee_start(false) != ESP_OK) return;
    publish_network_event(GATEWAY_EVENT_STACK_READY);
    xTaskCreate(discovery_task, "zb_discovery", 4096, NULL, 5, NULL);
    esp_zigbee_launch_mainloop();
}

void zigbee_gateway_start(void)
{
    xTaskCreate(zigbee_task, "zigbee_main", 6144, NULL, 5, NULL);
}

void zigbee_gateway_set_permit_join(uint8_t seconds)
{
    if (!esp_zigbee_lock_acquire(pdMS_TO_TICKS(GATEWAY_ZIGBEE_LOCK_TIMEOUT_MS))) {
        gateway_event_t event = gateway_event_base(GATEWAY_EVENT_WARNING, NULL);
        strncpy(event.data.text.value, "permit command rejected: Zigbee lock timeout", sizeof(event.data.text.value) - 1U);
        gateway_event_publish(&event);
        return;
    }
    if (seconds == 0U) ezb_bdb_close_network(); else ezb_bdb_open_network(seconds);
    esp_zigbee_lock_release();
}
