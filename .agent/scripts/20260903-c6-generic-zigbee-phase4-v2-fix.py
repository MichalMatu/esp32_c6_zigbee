from pathlib import Path

p = Path('main/zigbee_gateway.c')
text = p.read_text()
for signature in (
    'static bool schedule_basic(device_slot_t *slot, uint8_t endpoint)\n',
    'static void handle_check_in(ezb_zcl_poll_control_check_in_message_t *message)\n',
    'static void discovery_task(void *arg)\n',
):
    doubled = signature + signature
    if doubled not in text:
        raise SystemExit(f'expected duplicated generated signature not found: {signature!r}')
    text = text.replace(doubled, signature, 1)
p.write_text(text)

p = Path('tests/host/test_gateway_reporting_policy.c')
text = p.read_text()
old = 'GATEWAY_MEAS_HUMIDITY, 60500U, 300900U, 1.005, &plan)'
new = 'GATEWAY_MEAS_HUMIDITY, 60500U, 300900U, 1.006, &plan)'
if old not in text:
    raise SystemExit('expected humidity rounding test input not found')
p.write_text(text.replace(old, new, 1))

print('phase4 v2 fixes applied')
