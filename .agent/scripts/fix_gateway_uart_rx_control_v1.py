from pathlib import Path

ROOT = Path('.')

# HELLO now truthfully advertises the implemented PERMIT_JOIN feature.
test_path = ROOT / 'tests/host/test_gateway_link_event_adapter.c'
test = test_path.read_text()
old = '    assert(hello.features == 0U);\n'
assert old in test
test = test.replace(old, '    assert(hello.features == GATEWAY_LINK_FEATURE_PERMIT_JOIN);\n', 1)
test_path.write_text(test)

# Ensure partial startup failure does not leave a live TX task/UART driver behind.
uart_path = ROOT / 'main/gateway_uart_link.c'
uart = uart_path.read_text()
old = '''    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,\n                    LINK_TX_TASK_PRIORITY, NULL) != pdPASS) {\n        uart_driver_delete(LINK_UART);\n        s_tx_queue = NULL;\n        return ESP_ERR_NO_MEM;\n    }\n    if (xTaskCreate(rx_task, "gateway_uart_rx", LINK_RX_TASK_STACK_BYTES, NULL,\n                    LINK_RX_TASK_PRIORITY, NULL) != pdPASS) {\n        ESP_LOGE(TAG, "failed to create GatewayLink RX task");\n        return ESP_ERR_NO_MEM;\n    }\n'''
new = '''    TaskHandle_t tx_handle = NULL;\n    if (xTaskCreate(tx_task, "gateway_uart_tx", LINK_TX_TASK_STACK_BYTES, NULL,\n                    LINK_TX_TASK_PRIORITY, &tx_handle) != pdPASS) {\n        uart_driver_delete(LINK_UART);\n        s_tx_queue = NULL;\n        return ESP_ERR_NO_MEM;\n    }\n    if (xTaskCreate(rx_task, "gateway_uart_rx", LINK_RX_TASK_STACK_BYTES, NULL,\n                    LINK_RX_TASK_PRIORITY, NULL) != pdPASS) {\n        ESP_LOGE(TAG, "failed to create GatewayLink RX task");\n        vTaskDelete(tx_handle);\n        uart_driver_delete(LINK_UART);\n        s_tx_queue = NULL;\n        return ESP_ERR_NO_MEM;\n    }\n'''
assert old in uart
uart = uart.replace(old, new, 1)
uart_path.write_text(uart)

for path in [test_path, uart_path]:
    assert b'\x00' not in path.read_bytes()
