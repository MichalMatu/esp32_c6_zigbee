# LiteGraph absorption guide — ESP32-C6 Zigbee extension

## Goal
Move this repository into the LiteGraph controller as one independently buildable ESP32-C6 firmware module, without changing runtime behavior during the move. Recommended destination: `firmware/extensions/zigbee-c6/`.

The C6 remains a separate firmware image. Monorepo integration provides one source tree, shared contracts, common CI/release orchestration and one development context; it does not merge C6 and S3 binaries.

## Copy
Copy the tracked C6 build inputs and module content: root ESP-IDF project files, `main/`, `tests/`, `scripts/`, `tests/zigbee_device_emulator/`, `module.json`, active docs, and any partition/configuration source required by the standalone build.

## Do not copy as module content
Do not import `.git/`, `.agent/`, Local Agent task/run evidence, generated `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`, host temporary files, or the standalone GitHub Actions workflow verbatim. Recreate monorepo CI and call the canonical module scripts from there.

`AGENTS.md` mixes stable architecture with standalone-repository Local Agent rules. Carry forward the architectural invariants, but rewrite repository/binding/control-branch instructions for the future monorepo instead of copying them verbatim.

## C6-private responsibilities
Keep Zigbee coordinator lifecycle/storage, IEEE-first identity, short-address routing, bounded discovery/work scheduling, ZCL/IAS handling, reporting/binding, Zigbee commands, local wired inputs such as SCD4x, C6 GatewayLink runtime/backends, console, emulator and C6-specific tests inside the C6 module.

Do not move Wi-Fi, BLE, web/UI, LiteGraph application logic, Matter, Thread or external-RCP responsibilities onto C6 during migration.

## Shared contract later
GatewayLink v2 is the only active contract. During the initial import, keep its existing C6 implementation unchanged. After the nested module passes all gates, pure protocol definitions/tests may be extracted to a neutral shared monorepo location only if both processors consume one canonical implementation. Do not create two hand-maintained protocol copies and do not add a v1 compatibility shim.

## Future I2C peer boundary
The C6 side is already implemented:
- C6 master, I2C0;
- SDA GPIO1, SCL GPIO0, 400 kHz;
- SCD4x shares the bus at `0x62`;
- planned S3 slave address `0x42`;
- 20 ms transaction timeout and about 1 s absent-peer backoff;
- mailbox carries complete encoded GatewayLink v2 frames;
- opcodes: `0x01 WRITE_FRAME`, `0x02 PENDING_LENGTH`, `0x03 READ_FRAME`.

The future S3 side must implement the complementary slave/mailbox plus GatewayLink v2 semantics. True physical C6↔S3 I2C communication is still unverified. Verified today: C6 I2C backend with S3 absent while Zigbee and the shared SCD41 remain healthy.

## Migration sequence
1. Record the export checkpoint SHA from this repository.
2. Copy the module to `firmware/extensions/zigbee-c6/` with no runtime redesign.
3. Make only nesting/path/build-system adjustments.
4. Run `./scripts/run_host_tests.sh` and `./scripts/verify_migration_ready.sh` from the nested module.
5. Build UART-default C6 firmware with ESP-IDF v5.5.4.
6. Build the I2C-backend configuration.
7. Build `tests/zigbee_device_emulator`.
8. Flash both C6 boards without erasing persisted Zigbee storage and rerun the preserved-storage UART regression.
9. Only after the imported C6 behavior is proven, implement the S3 I2C slave/mailbox.
10. Validate real C6↔S3 traffic: HELLO/ACK + `peer=1`, C6→S3 measurements, S3→C6 control, shared-bus SCD4x stability, disconnect/backoff and reconnect recovery without C6 NVS erase.
11. Keep UART as fallback until real I2C E2E plus bounded soak pass.

## Recovery checkpoints
- `c6-sonoff-stable-2026-09-02` → `0d64fb03164d3bcb9f5cddd639977b4027bc581f`
- `c6-gatewaylink-stable-2026-09-03` → `a4b1f629c1286d631ac208515b71aeeaa7c44b23`
- dual-C6 IAS/rejoin source → `109a01f32d3bbc5c2ce2799ccbc8946a717b0e7a`
- post-refactor physically tested firmware → `f13b293be2de6b1601d179568424e0046d6219a7`
- pre-migration docs closure → `5801964144ccc8f825c4d4548daca4a1e526937c`

Do not move historical stable tags.

## Post-import checklist
- [ ] nested module host tests pass
- [ ] `verify_migration_ready.sh` passes from outside the module directory
- [ ] UART build passes
- [ ] I2C build passes
- [ ] emulator build passes
- [ ] generated build/sdkconfig files are not committed
- [ ] GatewayLink remains v2
- [ ] UART fallback remains available
- [ ] no Zigbee NVS erase during migration validation
- [ ] gateway identity resolves as `40:4C:CA:5D:0A:00`
- [ ] emulator identity resolves as `40:4C:CA:5D:01:D8`
- [ ] preserved-rejoin IAS false/true regression passes after import
- [ ] no claim of C6↔S3 I2C verification until a real S3 slave passes E2E
