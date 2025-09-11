# ESP32 BLE Web Controller

ESP32 BLE GATT client with a web interface.  
Scan BLE devices, pair, and reconnect automatically. Clean UI with animations.

## Features
- BLE GATT client (ESP32)
- Web interface hosted via ESP32 Access Point
- Persistent BLE pairing in NVS
- Re-pair functionality
- Modern dark UI with animations

## Requirements
- Arduino IDE 2.x
- ESP32 board package 3.3.0+
- Libraries:
  - WiFi
  - WebServer
  - Preferences
  - BLE

## Setup
1. Upload `ESP32_BLE_Web.ino` to your ESP32.
2. Connect to WiFi AP `ESP32_AP` (password: `12345678`).
3. Open browser at `192.168.4.1` and use the web interface.

## License
[MIT](LICENSE)
