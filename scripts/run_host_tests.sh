#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CC=${CC:-cc}
COMMON=(-std=c11 -Wall -Wextra -Werror -pedantic -Imain)
run() { echo "[host] $1"; shift; "$@"; }
run source-hygiene python3 tests/host/test_no_embedded_nul.py
run discovery-claim-source python3 tests/host/test_discovery_claim_source.py
run zcl "$CC" "${COMMON[@]}" -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zcl_value.c main/gateway_zcl_value.c -lm -o /tmp/test_gateway_zcl_value
/tmp/test_gateway_zcl_value
run device-state "$CC" "${COMMON[@]}" tests/host/test_gateway_device_state.c main/gateway_device_state.c -o /tmp/test_gateway_device_state
/tmp/test_gateway_device_state
run inputs "$CC" "${COMMON[@]}" tests/host/test_gateway_inputs.c main/gateway_inputs.c -o /tmp/test_gateway_inputs
/tmp/test_gateway_inputs
run zigbee-input "$CC" "${COMMON[@]}" -DGATEWAY_ZCL_HOST_TEST tests/host/test_gateway_zigbee_input.c main/gateway_zigbee_input.c main/gateway_zcl_value.c main/gateway_reporting_policy.c main/gateway_inputs.c -lm -o /tmp/test_gateway_zigbee_input
/tmp/test_gateway_zigbee_input
run command-policy "$CC" "${COMMON[@]}" tests/host/test_gateway_command_policy.c main/gateway_command_policy.c -lm -o /tmp/test_gateway_command_policy
/tmp/test_gateway_command_policy
run link-protocol "$CC" "${COMMON[@]}" tests/host/test_gateway_link_protocol.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_protocol
/tmp/test_gateway_link_protocol
run link-event-adapter "$CC" "${COMMON[@]}" tests/host/test_gateway_link_event_adapter.c main/gateway_link_event_adapter.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_event_adapter
/tmp/test_gateway_link_event_adapter
run link-stream "$CC" "${COMMON[@]}" tests/host/test_gateway_link_stream.c main/gateway_link_stream.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_stream
/tmp/test_gateway_link_stream
run link-snapshot-cache "$CC" "${COMMON[@]}" tests/host/test_gateway_link_snapshot_cache.c main/gateway_link_snapshot_cache.c -o /tmp/test_gateway_link_snapshot_cache
/tmp/test_gateway_link_snapshot_cache
run link-control "$CC" "${COMMON[@]}" tests/host/test_gateway_link_control.c main/gateway_link_control.c main/gateway_link_protocol.c main/gateway_link_frame.c -lm -o /tmp/test_gateway_link_control
/tmp/test_gateway_link_control
run link-e2e "$CC" "${COMMON[@]}" tests/host/test_gateway_link_e2e.c main/gateway_link_protocol.c main/gateway_link_frame.c main/gateway_link_stream.c main/gateway_link_control.c main/gateway_link_snapshot_cache.c main/gateway_inputs.c -lm -o /tmp/test_gateway_link_e2e
/tmp/test_gateway_link_e2e
run reporting-policy "$CC" "${COMMON[@]}" tests/host/test_gateway_reporting_policy.c main/gateway_reporting_policy.c -o /tmp/test_gateway_reporting_policy
/tmp/test_gateway_reporting_policy
run i2c-mailbox "$CC" "${COMMON[@]}" tests/host/test_gateway_i2c_mailbox.c main/gateway_i2c_mailbox.c -o /tmp/test_gateway_i2c_mailbox
/tmp/test_gateway_i2c_mailbox
echo '[host] all tests passed'
