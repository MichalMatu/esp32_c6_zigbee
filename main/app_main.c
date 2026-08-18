#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_events.h"
#include "gateway_transport.h"
#include "zigbee_gateway.h"

static void publish_console_message(const char *message)
{
    gateway_event_t event = {
        .source = GATEWAY_SOURCE_ZIGBEE,
        .kind = GATEWAY_EVENT_WARNING,
        .uptime_ms = gateway_uptime_ms(),
    };
    strncpy(event.data.text.value, message, sizeof(event.data.text.value) - 1U);
    gateway_event_publish(&event);
}

static void serial_command_task(void *arg)
{
    char line[32];
    publish_console_message("commands: help | permit <1..255> | permit 0");
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "help") == 0) {
            publish_console_message("commands: help | permit <1..255> | permit 0");
            continue;
        }
        if (strncmp(line, "permit ", 7U) == 0) {
            char *end = NULL;
            long seconds = strtol(line + 7, &end, 10);
            if (end != line + 7 && *end == '\0' && seconds >= 0L && seconds <= 255L) {
                zigbee_gateway_set_permit_join((uint8_t)seconds);
                continue;
            }
        }
        publish_console_message("invalid command; use help");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition("zb_storage"));
    gateway_events_init();
    gateway_transport_start();
    zigbee_gateway_start();
    xTaskCreate(serial_command_task, "serial_commands", 3072, NULL, 4, NULL);
}
