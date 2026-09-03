#!/usr/bin/env bash
set -euo pipefail

git fetch origin
git checkout integration/c6-s3-i2c-20260903
git reset --hard origin/integration/c6-s3-i2c-20260903
rm -f sdkconfig.old
[ -z "$(git status --short)" ]
[ "$(git rev-parse HEAD)" = "fe80910794c71eb67c792ca70e9f1803624a954f" ]

git fetch origin agent-control
git show origin/agent-control:.agent/scripts/20260903-c6-generic-zigbee-phase6-level-v1.py > /tmp/c6-phase6-level-v3.py
python3 - <<'PY'
from pathlib import Path
p = Path('/tmp/c6-phase6-level-v3.py')
s = p.read_text()

# Replace the ambiguous gateway_zcl_value insertion with an exact On/Off-tail replacement.
start_marker = '''insert_before(
    "main/gateway_zcl_value.c",
    "    return false;\\n}",
'''
start = s.index(start_marker)
end_marker = '''

replace_once(
    "tests/host/test_gateway_zcl_value.c",
'''
end = s.index(end_marker, start)
replacement = r'''replace_once(
    "main/gateway_zcl_value.c",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF &&\n        read_u8(value, type, &u8) && u8 <= 1U) {\n        *kind = GATEWAY_MEAS_ON_OFF;\n        *unit = GATEWAY_UNIT_BOOLEAN;\n        *number = u8 != 0U;\n        return true;\n    }\n    return false;\n}",
    "    if (cluster == EZB_ZCL_CLUSTER_ID_ON_OFF && attribute == ZCL_ATTR_ON_OFF &&\n        read_u8(value, type, &u8) && u8 <= 1U) {\n        *kind = GATEWAY_MEAS_ON_OFF;\n        *unit = GATEWAY_UNIT_BOOLEAN;\n        *number = u8 != 0U;\n        return true;\n    }\n    if (cluster == EZB_ZCL_CLUSTER_ID_LEVEL_CONTROL &&\n        attribute == ZCL_ATTR_CURRENT_LEVEL &&\n        read_u8(value, type, &u8) && u8 <= 254U) {\n        *kind = GATEWAY_MEAS_LEVEL;\n        *unit = GATEWAY_UNIT_PERCENT;\n        *number = (double)u8 * 100.0 / 254.0;\n        return true;\n    }\n    return false;\n}",
)'''
s = s[:start] + replacement + s[end:]

# The real host test keeps its cluster list on one line and uses equality assertions.
zigbee_test_start = s.index('''replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "        0x0006U,\\n",
''')
zigbee_test_end = s.index('\n\n# Explicit wire mappings for new measurement and command kind.', zigbee_test_start)
zigbee_test_replacement = r'''replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "        0x0000U, 0x0001U, 0x0006U, 0x0402U, 0x0405U, 0x0b04U,\n",
    "        0x0000U, 0x0001U, 0x0006U, 0x0008U, 0x0402U, 0x0405U, 0x0b04U,\n",
)
replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "        GATEWAY_INPUT_CAP_ON_OFF |\n        GATEWAY_INPUT_CAP_TEMPERATURE |",
    "        GATEWAY_INPUT_CAP_ON_OFF |\n        GATEWAY_INPUT_CAP_LEVEL |\n        GATEWAY_INPUT_CAP_TEMPERATURE |",
)
replace_once(
    "tests/host/test_gateway_zigbee_input.c",
    "    assert(profile.commandable == GATEWAY_INPUT_CAP_ON_OFF);\n",
    "    assert(profile.commandable == (GATEWAY_INPUT_CAP_ON_OFF |\n        GATEWAY_INPUT_CAP_LEVEL));\n",
)'''
s = s[:zigbee_test_start] + zigbee_test_replacement + s[zigbee_test_end:]

old_send = 'ezb_err_t send_result = EZB_ERR_INVALID_ARG;'
if old_send not in s:
    raise SystemExit('send_result marker missing')
s = s.replace(old_send, 'ezb_err_t send_result = (ezb_err_t)1;', 1)
p.write_text(s)
PY

python3 /tmp/c6-phase6-level-v3.py
python3 tests/host/test_no_embedded_nul.py
git diff --check
git diff --stat

cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_command_policy.c main/gateway_command_policy.c -lm -o /tmp/test_gateway_command_policy && /tmp/test_gateway_command_policy
cc -std=c11 -Wall -Wextra -Werror -pedantic -DGATEWAY_ZCL_HOST_TEST -Imain tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c -lm -o /tmp/test_gateway_zcl_value && /tmp/test_gateway_zcl_value
cc -std=c11 -Wall -Wextra -Werror -pedantic -DGATEWAY_ZCL_HOST_TEST -Imain tests/host/test_gateway_zigbee_input.c main/gateway_zigbee_input.c main/gateway_zcl_value.c main/gateway_reporting_policy.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zigbee_input && /tmp/test_gateway_zigbee_input
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_protocol.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_protocol && /tmp/test_gateway_link_protocol
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_control.c main/gateway_link_control.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_control && /tmp/test_gateway_link_control
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_e2e.c main/gateway_link_protocol.c main/gateway_link_stream.c main/gateway_link_control.c main/gateway_link_snapshot_cache.c main/gateway_inputs.c -lm -o /tmp/test_gateway_link_e2e && /tmp/test_gateway_link_e2e

python3 tests/host/test_no_embedded_nul.py
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_device_state.c main/gateway_device_state.c -o /tmp/test_gateway_device_state && /tmp/test_gateway_device_state
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_inputs.c main/gateway_inputs.c -o /tmp/test_gateway_inputs && /tmp/test_gateway_inputs
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_event_adapter.c main/gateway_link_event_adapter.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_event_adapter && /tmp/test_gateway_link_event_adapter
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_stream.c main/gateway_link_stream.c main/gateway_link_protocol.c -lm -o /tmp/test_gateway_link_stream && /tmp/test_gateway_link_stream
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_link_snapshot_cache.c main/gateway_link_snapshot_cache.c -o /tmp/test_gateway_link_snapshot_cache && /tmp/test_gateway_link_snapshot_cache
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_reporting_policy.c main/gateway_reporting_policy.c -o /tmp/test_gateway_reporting_policy && /tmp/test_gateway_reporting_policy
cc -std=c11 -Wall -Wextra -Werror -pedantic -Imain tests/host/test_gateway_i2c_mailbox.c main/gateway_i2c_mailbox.c -o /tmp/test_gateway_i2c_mailbox && /tmp/test_gateway_i2c_mailbox

. ~/esp/esp-idf-v5.5.4/export.sh >/dev/null
idf.py set-target esp32c6
idf.py build
idf.py size
rm -f sdkconfig.old
python3 tests/host/test_no_embedded_nul.py
git diff --check

git add main tests docs
git diff --cached --check
git commit -m 'Add normalized Zigbee Level Control'
git push origin HEAD:integration/c6-s3-i2c-20260903
printf 'PHASE6_HEAD=%s\n' "$(git rev-parse HEAD)"
git status --short
