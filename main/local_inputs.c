#include "local_inputs.h"

#include "gateway_events.h"
#include "gateway_inputs.h"
#include "local_i2c_bus.h"
#include "scd4x_input.h"

esp_err_t local_inputs_start(void)
{
    const gateway_input_id_t bus = gateway_input_make(
        GATEWAY_SOURCE_LOCAL_I2C, "i2c-bus:0", 0U);
    const esp_err_t bus_result = local_i2c_bus_init();
    if (bus_result != ESP_OK) {
        gateway_event_warning_input(&bus, "local I2C bus initialization failed");
        return ESP_OK;
    }

    return scd4x_input_start();
}
