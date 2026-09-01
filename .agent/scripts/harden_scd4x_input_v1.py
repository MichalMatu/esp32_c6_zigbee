from pathlib import Path

p = Path('main/local_i2c_bus.h')
s = p.read_text()
s = s.replace(
    'esp_err_t local_i2c_bus_init(void);\nesp_err_t local_i2c_bus_add_device(\n',
    'esp_err_t local_i2c_bus_init(void);\nesp_err_t local_i2c_bus_probe(uint16_t address, uint32_t timeout_ms);\nesp_err_t local_i2c_bus_add_device(\n',
    1)
p.write_text(s)

p = Path('main/local_i2c_bus.c')
s = p.read_text()
s = s.replace('.sda_io_num = GPIO_NUM_1,\n        .scl_io_num = GPIO_NUM_0,',
              '.sda_io_num = (gpio_num_t)LOCAL_I2C_SDA_GPIO,\n        .scl_io_num = (gpio_num_t)LOCAL_I2C_SCL_GPIO,', 1)
marker = '''esp_err_t local_i2c_bus_add_device(
'''
probe = '''esp_err_t local_i2c_bus_probe(uint16_t address, uint32_t timeout_ms)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(s_bus, address, (int)timeout_ms);
}

'''
assert marker in s
s = s.replace(marker, probe + marker, 1)
p.write_text(s)

p = Path('main/scd4x_input.c')
s = p.read_text()
s = s.replace(
    '#define SCD4X_INPUT_I2C_SPEED_HZ 100000U\n#define SCD4X_INPUT_POLL_MS 1000U\n',
    '#define SCD4X_INPUT_I2C_SPEED_HZ 100000U\n#define SCD4X_INPUT_PROBE_TIMEOUT_MS 100U\n#define SCD4X_INPUT_POLL_MS 1000U\n',
    1)
old = '''    i2c_master_dev_handle_t i2c_device = NULL;
    gateway_input_id_t input = fallback_input();
    const esp_err_t add_result = local_i2c_bus_add_device(
        SCD4X_I2C_ADDR, SCD4X_INPUT_I2C_SPEED_HZ, &i2c_device);
    if (add_result != ESP_OK) {
        gateway_event_warning_input(&input, "SCD4x I2C device registration failed");
        vTaskDelete(NULL);
        return;
    }

    scd4x_t *sensor = NULL;
'''
new = '''    i2c_master_dev_handle_t i2c_device = NULL;
    gateway_input_id_t input = fallback_input();
    scd4x_t *sensor = NULL;
'''
assert old in s
s = s.replace(old, new, 1)
old = '''        if (sensor == NULL) {
            sensor = scd4x_init(i2c_device);
            if (sensor == NULL) {
                warning_throttled(
                    &input,
                    "SCD4x not responding on local I2C bus",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }

            uint16_t serial[3];
'''
new = '''        if (sensor == NULL) {
            if (local_i2c_bus_probe(SCD4X_I2C_ADDR, SCD4X_INPUT_PROBE_TIMEOUT_MS) != ESP_OK) {
                warning_throttled(
                    &input,
                    "SCD4x not present on local I2C bus",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }
            if (i2c_device == NULL) {
                const esp_err_t add_result = local_i2c_bus_add_device(
                    SCD4X_I2C_ADDR, SCD4X_INPUT_I2C_SPEED_HZ, &i2c_device);
                if (add_result != ESP_OK) {
                    gateway_event_warning_input(
                        &input, "SCD4x I2C device registration failed");
                    vTaskDelete(NULL);
                    return;
                }
            }

            sensor = scd4x_init(i2c_device);
            if (sensor == NULL) {
                warning_throttled(
                    &input,
                    "SCD4x initialization failed after successful I2C probe",
                    &last_warning_ms);
                vTaskDelay(pdMS_TO_TICKS(SCD4X_INPUT_INIT_RETRY_MS));
                continue;
            }

            uint16_t serial[3];
'''
assert old in s
s = s.replace(old, new, 1)
p.write_text(s)

p = Path('main/gateway_transport.c')
s = p.read_text()
old = '    case GATEWAY_EVENT_WARNING: ESP_LOGW(TAG, "%s", event->data.text.value); break;\n'
new = '''    case GATEWAY_EVENT_WARNING:
        if (event->input.id[0] != '\\0') {
            char input[80];
            format_input(&event->input, input, sizeof(input));
            ESP_LOGW(TAG, "%s: %s", input, event->data.text.value);
        } else {
            ESP_LOGW(TAG, "%s", event->data.text.value);
        }
        break;
'''
assert old in s
p.write_text(s.replace(old, new, 1))

p = Path('docs/ARCHITECTURE.md')
s = p.read_text()
s = s.replace(
    '- `scd4x_input.c/.h` adapts an SCD4x-family sensor into the protocol-neutral input contract. It owns SCD4x polling/recovery policy, not transport or Zigbee behavior.\n',
    '- `scd4x_input.c/.h` adapts an SCD4x-family sensor into the protocol-neutral input contract. It probes before driver initialization, throttles absence/read warnings, and owns SCD4x polling/recovery policy, not transport or Zigbee behavior.\n',
    1)
p.write_text(s)
