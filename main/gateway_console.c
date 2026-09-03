#include "gateway_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_events.h"
#include "gateway_link.h"
#include "zigbee_gateway.h"

static const char *COMMAND_HELP = "commands: help | link status | permit <1..255> | permit 0";

static void print_link_status(void)
{
    gateway_link_status_t status;
    if (!gateway_link_get_status(&status)) {
        printf("link status unavailable\n");
        fflush(stdout);
        return;
    }
    printf(
        "link backend=%s peer=%u tx=%" PRIu32 " rx=%" PRIu32
        " invalid=%" PRIu32 " queue=%" PRIu32 "/%" PRIu32
        " high=%" PRIu32 " drop=%" PRIu32 " short=%" PRIu32 "\n",
        status.backend, status.peer_ready ? 1U : 0U, status.tx_frames, status.rx_frames,
        status.rx_invalid_frames, status.tx_queue_depth, status.tx_queue_capacity,
        status.tx_queue_high_water, status.queue_dropped, status.short_writes);
    printf(
        "link last_tx_ms=%" PRIu32 " last_rx_ms=%" PRIu32
        " min_heap=%" PRIu32 " tx_stack_hwm=%" PRIu32
        " rx_stack_hwm=%" PRIu32 "\n",
        status.last_tx_ms, status.last_rx_ms, status.minimum_free_heap_bytes,
        status.tx_stack_high_water, status.rx_stack_high_water);
    fflush(stdout);
}

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
        if (strcmp(line, "link status") == 0) {
            print_link_status();
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
