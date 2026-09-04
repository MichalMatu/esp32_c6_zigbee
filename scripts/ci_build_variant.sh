#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
variant="${1:-}"

case "$variant" in
  uart)
    rm -rf build sdkconfig sdkconfig.old
    idf.py set-target esp32c6
    idf.py build
    ;;
  i2c)
    rm -rf build sdkconfig sdkconfig.old
    idf.py set-target esp32c6
    python3 - <<'PY'
from pathlib import Path
p = Path('sdkconfig')
s = p.read_text()
old = 'CONFIG_GATEWAY_LINK_BACKEND_UART=y'
new = '# CONFIG_GATEWAY_LINK_BACKEND_UART is not set\nCONFIG_GATEWAY_LINK_BACKEND_I2C=y'
if old not in s:
    raise SystemExit('default UART GatewayLink backend not found in sdkconfig')
p.write_text(s.replace(old, new, 1))
PY
    idf.py reconfigure
    idf.py build
    ;;
  emulator)
    rm -rf tests/zigbee_device_emulator/build \
           tests/zigbee_device_emulator/sdkconfig \
           tests/zigbee_device_emulator/sdkconfig.old
    idf.py -C tests/zigbee_device_emulator set-target esp32c6
    idf.py -C tests/zigbee_device_emulator build
    ;;
  *)
    echo "usage: $0 {uart|i2c|emulator}" >&2
    exit 2
    ;;
esac
