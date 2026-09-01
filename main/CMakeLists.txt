idf_component_register(
    SRCS
        "app_main.c"
        "gateway_device_state.c"
        "gateway_events.c"
        "gateway_reporting_policy.c"
        "gateway_transport.c"
        "gateway_zcl_value.c"
        "zigbee_gateway.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer
)
