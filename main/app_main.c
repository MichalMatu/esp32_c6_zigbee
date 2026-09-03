#include "esp_err.h"
#include "nvs_flash.h"

#include "gateway_console.h"
#include "gateway_events.h"
#include "gateway_transport.h"
#include "gateway_link.h"
#include "local_inputs.h"
#include "zigbee_gateway.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition("zb_storage"));
    ESP_ERROR_CHECK(gateway_events_init() ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(gateway_link_start());
    ESP_ERROR_CHECK(gateway_transport_start());
    ESP_ERROR_CHECK(local_inputs_start());
    ESP_ERROR_CHECK(zigbee_gateway_start());
    ESP_ERROR_CHECK(gateway_console_start());
}
