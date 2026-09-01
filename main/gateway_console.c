#include "gateway_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_events.h"
#include "zigbee_gateway.h"

static const char *COMMAND_HELP = "commands: help | permit <1..255> | permit 0";

static void serial_command_task(void *arg)
{
    (void)arg;
    char line[32];
    gateway_event_warning(NULL, COMMAND_HELP);
    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "help") == 0) {
            gateway_event_warning(NULL, COMMAND_HELP);
            continue;
        }
        if (strncmp(line, "permit ", 7U) == 0) {
            char *end = NULL;
            const long seconds = strtol(line + 7, &end, 10);
            if (end != line + 7 && *end == '\0' && seconds >= 0L && seconds <= 255L) {
                (void)zigbee_gateway_set_permit_join((uint8_t)seconds);
                continue;
            }
        }
        gateway_event_warning(NULL, "invalid command; use help");
    }
}

esp_err_t gateway_console_start(void)
{
    return xTaskCreate(
        serial_command_task, "serial_commands", 3072, NULL, 4, NULL
    ) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
