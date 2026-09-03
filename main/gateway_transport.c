#include "gateway_transport.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_events.h"
#include "gateway_link.h"

static const char *TAG = "gateway_transport";

static void format_device(const gateway_device_id_t *device, char *out, size_t out_size)
{
    if (device->ieee_valid) {
        snprintf(out, out_size, "%02x%02x%02x%02x%02x%02x%02x%02x/0x%04x",
                 device->ieee[7], device->ieee[6], device->ieee[5], device->ieee[4],
                 device->ieee[3], device->ieee[2], device->ieee[1], device->ieee[0], device->short_addr);
    } else {
        snprintf(out, out_size, "short/0x%04x", device->short_addr);
    }
}

static void format_ieee(const gateway_device_id_t *device, char *out, size_t out_size)
{
    if (!device->ieee_valid) {
        snprintf(out, out_size, "unknown");
        return;
    }
    snprintf(out, out_size, "%02x%02x%02x%02x%02x%02x%02x%02x",
             device->ieee[7], device->ieee[6], device->ieee[5], device->ieee[4],
             device->ieee[3], device->ieee[2], device->ieee[1], device->ieee[0]);
}

static const char *measurement_name(gateway_measurement_kind_t kind)
{
    static const char *const names[] = {
        "temperature", "humidity", "illuminance", "occupancy", "co2", "battery_voltage",
        "battery_percent", "mains_voltage", "voltage", "current", "power", "energy", "on_off",
    };
    return kind < (sizeof(names) / sizeof(names[0])) ? names[kind] : "unknown";
}

static const char *unit_name(gateway_unit_t unit)
{
    static const char *const names[] = {"", "C", "%", "log-lux", "ppm", "V", "A", "W", "kWh", "bool"};
    return unit < (sizeof(names) / sizeof(names[0])) ? names[unit] : "";
}

static void format_input(const gateway_input_id_t *input, char *out, size_t out_size)
{
    const char *id = input->id[0] == '\0' ? "unknown" : input->id;
    snprintf(out, out_size, "%s/%s/ch%u",
             gateway_input_source_name(input->source), id, input->channel);
}

static void format_short_addr(uint16_t short_addr, char *out, size_t out_size)
{
    if (short_addr == 0xffffU) {
        snprintf(out, out_size, "unknown");
    } else {
        snprintf(out, out_size, "0x%04x", short_addr);
    }
}

static void format_clusters(const uint16_t *clusters, uint8_t count, char *out, size_t out_size)
{
    size_t used = 0U;
    for (uint8_t i = 0; i < count && used + 7U < out_size; ++i) {
        used += (size_t)snprintf(out + used, out_size - used, "%s%04x", i == 0U ? "" : ",", clusters[i]);
    }
    if (count == 0U) snprintf(out, out_size, "-");
}

static void log_event(const gateway_event_t *event)
{
    char device[32];
    char ieee[17];
    format_device(&event->device, device, sizeof(device));
    format_ieee(&event->device, ieee, sizeof(ieee));
    switch (event->kind) {
    case GATEWAY_EVENT_STACK_READY: ESP_LOGI(TAG, "zigbee stack ready; endpoint=1, storage=zb_storage"); break;
    case GATEWAY_EVENT_NETWORK_FORMED: ESP_LOGI(TAG, "network formed; joining opens for 180 seconds"); break;
    case GATEWAY_EVENT_NETWORK_RESTORED: ESP_LOGI(TAG, "persisted coordinator network restored; joining remains closed"); break;
    case GATEWAY_EVENT_PERMIT_JOIN: ESP_LOGI(TAG, "permit join duration=%u", event->data.permit.duration); break;
    case GATEWAY_EVENT_DEVICE_ANNOUNCE: ESP_LOGI(TAG, "ZIGBEE_DEVICE_ANNOUNCE %s", device); break;
    case GATEWAY_EVENT_DEVICE_REJOIN:
    {
        char old_short[10];
        char new_short[10];
        format_short_addr(event->data.rejoin.old_short_addr, old_short, sizeof(old_short));
        format_short_addr(event->data.rejoin.new_short_addr, new_short, sizeof(new_short));
        ESP_LOGI(TAG, "ZIGBEE_DEVICE_REJOIN ieee=%s old_short=%s new_short=%s", ieee,
                 old_short, new_short);
        break;
    }
    case GATEWAY_EVENT_DEVICE_LEAVE_RESET:
        ESP_LOGI(TAG, "ZIGBEE_DEVICE_LEAVE_RESET ieee=%s short=0x%04x leave_type=%u retained=%s",
                 ieee, event->device.short_addr, event->data.leave.leave_type,
                 event->data.leave.record_retained ? "true" : "false");
        break;
    case GATEWAY_EVENT_DEVICE_LEAVE_REJOIN:
        ESP_LOGI(TAG, "ZIGBEE_DEVICE_LEAVE_REJOIN ieee=%s short=0x%04x leave_type=%u retained=%s",
                 ieee, event->device.short_addr, event->data.leave.leave_type,
                 event->data.leave.record_retained ? "true" : "false");
        break;
    case GATEWAY_EVENT_DEVICE_LEAVE_UNKNOWN:
        ESP_LOGW(TAG, "ZIGBEE_DEVICE_LEAVE_UNKNOWN ieee=%s short=0x%04x leave_type=0x%02x retained=%s",
                 ieee, event->device.short_addr, event->data.leave.leave_type,
                 event->data.leave.record_retained ? "true" : "false");
        break;
    case GATEWAY_EVENT_DEVICE_UPDATE:
        ESP_LOGI(TAG, "ZIGBEE_DEVICE_UPDATE %s status=0x%02x tc_action=0x%02x",
                 device, event->data.device_update.status, event->data.device_update.tc_action);
        break;
    case GATEWAY_EVENT_DEVICE_AUTHORIZED:
        ESP_LOGI(TAG, "ZIGBEE_DEVICE_AUTHORIZED %s type=0x%02x status=0x%02x",
                 device, event->data.authorization.type, event->data.authorization.status);
        break;
    case GATEWAY_EVENT_DEVICE_UNAVAILABLE: ESP_LOGW(TAG, "ZIGBEE_DEVICE_UNAVAILABLE %s (not an authoritative offline state)", device); break;
    case GATEWAY_EVENT_DEVICE_CHECK_IN: ESP_LOGI(TAG, "ZIGBEE_DEVICE_CHECK_IN %s; fast poll requested", device); break;
    case GATEWAY_EVENT_BINDING:
        ESP_LOGI(TAG, "binding %s ep=%u cluster=0x%04x status=0x%02x", device, event->endpoint,
                 event->data.binding.cluster_id, event->data.binding.status);
        break;
    case GATEWAY_EVENT_ENDPOINT:
    {
        char input[6U * GATEWAY_MAX_DESCRIPTOR_CLUSTERS + 1U];
        char output[6U * GATEWAY_MAX_DESCRIPTOR_CLUSTERS + 1U];
        format_clusters(event->data.endpoint_desc.input_clusters, event->data.endpoint_desc.input_copied, input, sizeof(input));
        format_clusters(event->data.endpoint_desc.output_clusters, event->data.endpoint_desc.output_copied, output, sizeof(output));
        ESP_LOGI(TAG, "descriptor %s ep=%u profile=0x%04x device=0x%04x in=%u[%s] out=%u[%s]", device, event->endpoint,
                 event->data.endpoint_desc.profile_id, event->data.endpoint_desc.device_id,
                 event->data.endpoint_desc.input_count, input, event->data.endpoint_desc.output_count, output);
        break;
    }
    case GATEWAY_EVENT_BASIC: ESP_LOGI(TAG, "basic %s %s=%s", device, event->data.text.key, event->data.text.value); break;
    case GATEWAY_EVENT_REPORTING_CONFIG:
        ESP_LOGI(TAG, "reporting %s ep=%u cluster=0x%04x attr=0x%04x status=0x%02x request=%" PRIu32 " result=%u",
                 device, event->endpoint, event->data.reporting.cluster_id,
                 event->data.reporting.attribute_id, event->data.reporting.status,
                 event->data.reporting.request_id, (unsigned)event->data.reporting.result);
        break;
    case GATEWAY_EVENT_INPUT_AVAILABLE:
    case GATEWAY_EVENT_INPUT_UNAVAILABLE:
    {
        char input[80];
        format_input(&event->input, input, sizeof(input));
        ESP_LOGI(TAG,
                 "input %s %s manufacturer=%s model=%s read=0x%08" PRIx32
                 " report=0x%08" PRIx32 " config=0x%08" PRIx32
                 " command=0x%08" PRIx32,
                 event->kind == GATEWAY_EVENT_INPUT_AVAILABLE ? "available" : "unavailable",
                 input, event->data.input_desc.manufacturer, event->data.input_desc.model,
                 (uint32_t)event->data.input_desc.profile.readable,
                 (uint32_t)event->data.input_desc.profile.reportable,
                 (uint32_t)event->data.input_desc.profile.configurable,
                 (uint32_t)event->data.input_desc.profile.commandable);
        break;
    }
    case GATEWAY_EVENT_MEASUREMENT:
    {
        char input[80];
        format_input(&event->input, input, sizeof(input));
        ESP_LOGI(TAG, "measurement %s %s=%.3f %s",
                 input, measurement_name(event->data.measurement.kind),
                 event->data.measurement.value, unit_name(event->data.measurement.unit));
        break;
    }
    case GATEWAY_EVENT_RAW_ATTRIBUTE:
        ESP_LOGI(TAG, "raw %s ep=%u cluster=0x%04x attr=0x%04x type=0x%02x bytes=%u/%u truncated=%s",
                 device, event->endpoint, event->data.raw.cluster_id, event->data.raw.attribute_id,
                 event->data.raw.zcl_type, event->data.raw.copied_length, event->data.raw.original_length,
                 event->data.raw.truncated ? "true" : "false");
        if (event->data.raw.copied_length != 0U) {
            ESP_LOG_BUFFER_HEX(TAG, event->data.raw.bytes, event->data.raw.copied_length);
        }
        break;
    case GATEWAY_EVENT_WARNING:
        if (event->input.id[0] != '\0') {
            char input[80];
            format_input(&event->input, input, sizeof(input));
            ESP_LOGW(TAG, "%s: %s", input, event->data.text.value);
        } else {
            ESP_LOGW(TAG, "%s", event->data.text.value);
        }
        break;
    default: break;
    }
}

static void gateway_transport_task(void *arg)
{
    gateway_event_t event;
    for (;;) {
        if (gateway_event_receive(&event, pdMS_TO_TICKS(1000))) {
            log_event(&event);
            gateway_link_publish_event(&event);
        }
        const uint32_t link_dropped = gateway_link_take_dropped();
        if (link_dropped != 0U) {
            ESP_LOGW(TAG, "dropped %" PRIu32 " GatewayLink messages because the TX queue was full", link_dropped);
        }
        uint32_t dropped = gateway_event_take_dropped();
        if (dropped != 0U) {
            ESP_LOGW(TAG, "dropped %" PRIu32 " gateway events because the 16-entry queue was full", dropped);
        }
    }
}

esp_err_t gateway_transport_start(void)
{
    return xTaskCreate(
        gateway_transport_task, "gateway_transport", 4096, NULL, 5, NULL
    ) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
