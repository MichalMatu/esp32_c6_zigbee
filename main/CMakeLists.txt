idf_component_register(
    SRCS
        "app_main.c"
        "gateway_console.c"
        "gateway_device_state.c"
        "gateway_events.c"
        "gateway_inputs.c"
        "gateway_reporting_policy.c"
        "gateway_transport.c"
        "gateway_zcl_value.c"
        "local_i2c_bus.c"
        "local_inputs.c"
        "scd4x_input.c"
        "zigbee_gateway.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer esp_driver_i2c
)
