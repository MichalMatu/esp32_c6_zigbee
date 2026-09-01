#include "gateway_events.h"

#include <stdatomic.h>

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
