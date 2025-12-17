# iot-agriculture
Repository for CT5061 IOT Development Assessment.

## Overview

An Arduino UNO R4 WiFi-based smart irrigation controller with:

- Wi‑Fi connectivity and time sync via NTP
- DHT11 temperature/humidity sensor
- Analogue water level sensor
- Relay-driven pump with hysteresis control
- 16x2 I2C LCD status display
- Optional ArduCAM OV2640 JPEG snapshot (HTTP variant only)

Two firmware variants are provided:

- HTTP Dashboard: [src/iot-agriculture.ino](src/iot-agriculture.ino)
- MQTT Variant: [src/iot-agriculture-mqtt.ino](src/iot-agriculture-mqtt.ino)

The HTTP dashboard suits a local web UI. The MQTT variant suits integration with a broker and
dedicated farming platforms such as Farmobile, CropX or FarmX.

## Hardware

- Arduino UNO R4 WiFi
- DHT11 on digital pin D5
- Water level sensor on analogue pin A0
- Relay module controlling the pump on digital pin D7 (active high)
- 16x2 I2C LCD at address 0x27 (SDA=A4, SCL=A5)
- Optional ArduCAM Mini OV2640, CS on D10 (SPI)

## Folder Structure

- src/: Sketches and headers
	- iot-agriculture.ino — HTTP dashboard
	- iot-agriculture-mqtt.ino — MQTT-based telemetry/control
	- index_page.h — Embedded HTML for the HTTP dashboard
	- arduino_secrets.h — Define Wi‑Fi/MQTT/pump settings (user-provided)

## Common Configuration

At minimum, [src/arduino_secrets.h](src/arduino_secrets.h) should define:

```cpp
#define SECRET_SSID "your-ssid"
#define SECRET_PASS "your-wifi-password"

// Target water level percentage (0–100)
#define SECRET_TARGET_LEVEL 60

// Optional: hysteresis to avoid rapid toggling (default 5)
//#define SECRET_PUMP_HYSTERESIS 5
```

## HTTP Dashboard Variant

Sketch: [src/iot-agriculture.ino](src/iot-agriculture.ino)

### Endpoints

- `/` — Dashboard HTML
- `/status` — `{ connected, ip, cameraDetected }`
- `/sensor` — `{ temperature, humidity, level, pump }`
- `/time` — `{ datetime }` (NTP-based)
- `/image` — `image/jpeg` snapshot from ArduCAM (if enabled)

### Libraries

- WiFiS3, UnoR4WiFi_WebServer
- NTPClient, LiquidCrystal I2C, DHT
- SPI, ArduCAM (UNO R4 compatible fork)

### Secrets

The HTTP dashboard uses Basic Authentication. Define the credentials in [src/arduino_secrets.h](src/arduino_secrets.h):

```cpp
#define SECRET_BASIC_USER "admin"
#define SECRET_BASIC_PASS "a-strong-passphrase"
```

## MQTT Variant

Sketch: [src/iot-agriculture-mqtt.ino](src/iot-agriculture-mqtt.ino)

### Additional Secrets

```cpp
#define SECRET_MQTT_HOST "broker.example.com"
#define SECRET_MQTT_PORT 1883
// Optional auth
//#define SECRET_MQTT_USER "user"
//#define SECRET_MQTT_PASS "pass"
// Optional base topic (default: "iot/agriculture")
//#define SECRET_MQTT_BASETOPIC "iot/agriculture"
```

### Libraries

- WiFiS3, PubSubClient
- NTPClient, LiquidCrystal I2C, DHT

### Topics

Base topic: `iot/agriculture/<DEVICE_ID>` where `<DEVICE_ID>` is the MAC without colons (e.g. `A1B2C3D4E5F6`).

- `<base>/status/connected` (retained): `true` | `false`
- `<base>/status/ip` (retained): IP address string
- `<base>/pump/state` (retained): `on` | `off`
- `<base>/sensor`: JSON payload

Example payload:

```json
{
	"temperature": 21.4,
	"humidity": 52,
	"level": 63,
	"pump": false,
	"mode": "auto",
	"time": "12:34"
}
```

### Commands

- `<base>/pump/cmd`: `auto` | `on` | `off`

## Uploading

The sketches are built in the Arduino IDE using the Arduino UNO R4 WiFi board. Required libraries are installed via the Library Manager. Configuration is provided in [src/arduino_secrets.h](src/arduino_secrets.h). After configuration, the selected sketch is compiled and uploaded to the device.

## Pump Logic

- Automatic mode keeps the pump on until level rises above `TARGET + HYSTERESIS`, then turns off.
- When off, it turns on only when level falls strictly below `TARGET`.
- MQTT variant also supports `on/off/auto` commands.

## Temperature warning

- The dashboard provides a simple temperature status. When the measured temperature exceeds 30°C (RHS upper limit for many UK crops) the `/sensor` endpoint returns a non-null `warning` string and the web UI displays a red warning; otherwise the dashboard shows "Good". This gives a quick visual cue for potentially harmful heat conditions.

## Notes & Troubleshooting

- If using the camera on UNO R4, install the UNO R4‑compatible ArduCAM fork and keep the JPEG size definition small.
- Ensure the LCD I2C address is 0x27; adjust if the module uses a different address.
- For MQTT, verify that the broker is reachable from the device network and that credentials are correct.

 

