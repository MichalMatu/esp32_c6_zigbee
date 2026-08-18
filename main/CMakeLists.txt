idf_component_register(
    SRCS "app_main.c" "gateway_events.c" "gateway_transport.c" "zigbee_gateway.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_timer
)
