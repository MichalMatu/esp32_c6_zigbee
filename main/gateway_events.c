#include "gateway_events.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static StaticQueue_t s_queue_storage;
static uint8_t s_queue_buffer[GATEWAY_EVENT_QUEUE_DEPTH * sizeof(gateway_event_t)];
static QueueHandle_t s_queue;
static atomic_uint_fast32_t s_dropped;

bool gateway_events_init(void)
{
    s_queue = xQueueCreateStatic(
        GATEWAY_EVENT_QUEUE_DEPTH,
        sizeof(gateway_event_t),
        s_queue_buffer,
        &s_queue_storage
    );
    atomic_store(&s_dropped, 0U);
    return s_queue != NULL;
}

gateway_event_t gateway_event_make(
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

bool gateway_event_warning(
    const gateway_device_id_t *device, const char *text)
{
    gateway_event_t event = gateway_event_make(GATEWAY_EVENT_WARNING, device);
    if (text != NULL) {
        strncpy(event.data.text.value, text, sizeof(event.data.text.value) - 1U);
    }
    return gateway_event_publish(&event);
}

bool gateway_event_publish(const gateway_event_t *event)
{
    if (s_queue == NULL || xQueueSend(s_queue, event, 0) != pdPASS) {
        atomic_fetch_add(&s_dropped, 1U);
        return false;
    }
    return true;
}

bool gateway_event_receive(gateway_event_t *event, uint32_t timeout_ticks)
{
    return s_queue != NULL && xQueueReceive(s_queue, event, timeout_ticks) == pdPASS;
}

uint32_t gateway_event_take_dropped(void)
{
    return atomic_exchange(&s_dropped, 0U);
}

uint32_t gateway_uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}
