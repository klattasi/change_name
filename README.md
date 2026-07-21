# BATT_01

ESP32 firmware (PlatformIO) that reads DC voltage/current from a **PZEM-017**
meter over RS485 Modbus and broadcasts the readings over WiFi (WebSocket) and
BLE. A small web dashboard served from SPIFFS shows live values.

## Features

- **RS485 / Modbus** — polls PZEM-017 input registers (voltage, current)
  every 5 s via [ModbusMaster](https://github.com/4-20ma/ModbusMaster), with
  MAX13487 DE/RE direction control.
- **WiFi Access Point** — opens an open (no password) AP so a phone/laptop
  can connect directly, no internet/router required.
- **Web dashboard** — `data/index.html` served from SPIFFS at `/`, updates
  live over a `/ws` WebSocket (`{"v":..,"i":..}` JSON).
- **BLE** — advertises as `BATT_01` with a GATT service exposing voltage and
  current characteristics (read + notify).

## Hardware

| Signal        | ESP32 GPIO |
|---------------|-----------:|
| RS485 TX (UART1) | 17      |
| RS485 RX (UART1) | 16      |
| RS485 DE/RE       | 4       |

- Board: `esp32doit-devkit-v1`
- Modbus slave address: `0x01` (PZEM-017 default)
- Modbus serial: 9600 baud, 8N2

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```sh
# Build and upload firmware
pio run -t upload

# Upload the web dashboard to SPIFFS
pio run -t uploadfs

# Open serial monitor (115200 baud)
pio device monitor
```

## Usage

1. Flash firmware and filesystem as above.
2. Connect to the WiFi AP **`WIFI_BATT_01`** (no password).
3. Browse to `http://<esp32-ap-ip>/` (default AP IP is usually
   `192.168.4.1`) to view live voltage/current.
4. Alternatively, connect via BLE to device **`BATT_01`** and subscribe to
   the voltage/current characteristics.

## Project Structure

```
src/main.cpp      Firmware: WiFi AP, WebSocket server, BLE, Modbus polling
data/index.html   Web dashboard (served from SPIFFS)
platformio.ini    Board, framework, and library configuration
```

## Dependencies

- [ModbusMaster](https://github.com/4-20ma/ModbusMaster) `^2.0.1`
- [ESPAsyncWebServer-esphome](https://github.com/esphome/ESPAsyncWebServer) `^3.1.0`
- [AsyncTCP-esphome](https://github.com/esphome/AsyncTCP) `^2.1.0`
