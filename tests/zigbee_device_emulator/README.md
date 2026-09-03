# Second-C6 Zigbee device emulator

This is a separate ESP-IDF application used as a deterministic Zigbee end-device peer for the production coordinator. It does not share production `main/` sources.

Current first profile: endpoint 1, Home Automation temperature sensor, Basic manufacturer/model metadata and Temperature Measurement server. The measured temperature changes every 10 seconds so coordinator-side Configure Reporting and normalized measurement handling can be exercised once the emulator is flashed on a distinct second ESP32-C6.

Build from this directory with ESP-IDF v5.5.4 and target `esp32c6`. Hardware flashing is intentionally a later gate after distinct Local Agent hardware resource identities are verified.
