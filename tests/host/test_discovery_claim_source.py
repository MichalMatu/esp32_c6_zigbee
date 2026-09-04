#!/usr/bin/env python3
from pathlib import Path
text = Path('main/zigbee_gateway_zcl.c').read_text()
if 'zigbee_gateway_schedule_active_discovery(slot)' not in text:
    raise SystemExit('ZCL callbacks no longer route discovery through the claim scheduler')
if 'DISCOVERY_ACTIVE_ENDPOINTS' in text:
    raise SystemExit('ZCL callback layer bypasses the private route-safe discovery scheduler')
print('discovery claim source invariant: PASS')
