# ESP32 port of the cooler panel (skeleton)

**Status: not compiled here** (no ESP-IDF toolchain in this environment). The portable core in
`../core` is tested on the host (`make -C firmware/panel test`, 37 checks against the real
drive-side Modbus and firmware-update code). This folder integrates it with ESP-IDF 5.x:

| file | content |
|---|---|
| `main/app_main.c` | tasks, BLE provisioning, Wi-Fi, mDNS, WebSocket `/ws`, MQTT/TLS, panel OTA, drive OTA, warranty upload |
| `main/bus_uart.c` | 2-wire bus on UART1 at 2400 bps with RX timeout as frame end |
| `partitions.csv` | two OTA slots + `drive_img` (drive image + Ed25519 signature) |

Build: `idf.py set-target esp32s3` (touch screen) or `esp32c3` (7-segment + keys), then `idf.py build flash`.
Placeholders to replace: manufacturer server URLs, MQTT broker, the drive-image public key, the
provisioning proof-of-possession (per-unit code printed on the label), GPIO numbers of the panel PHY.
