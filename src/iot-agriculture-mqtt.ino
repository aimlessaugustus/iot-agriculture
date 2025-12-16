/**
 * @file iot-agriculture-mqtt.ino
 * @brief IoT Agriculture device using MQTT instead of an HTTP dashboard.
 *
 * Features:
 *  - WiFi connectivity (WiFiS3)
 *  - MQTT telemetry via PubSubClient
 *  - NTP-based timekeeping
 *  - I2C LCD status display
 *  - DHT temperature/humidity sensor
 *  - Analogue water level sensor controlling a relay-driven pump
 *
 * Provide Wi-Fi and secrets in `arduino_secrets.h`:
 *   - SECRET_SSID, SECRET_PASS
 *   - SECRET_TARGET_LEVEL (0-100), optional SECRET_PUMP_HYSTERESIS
 *   - SECRET_MQTT_HOST, SECRET_MQTT_PORT, optional SECRET_MQTT_USER, SECRET_MQTT_PASS
 *   - optional SECRET_MQTT_BASETOPIC (defaults to "iot/agriculture")
 */

#include <WiFiS3.h>
#include "arduino_secrets.h"
// Use NTP instead of the RTC library for time
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
// MQTT telemetry and control
#include <PubSubClient.h>

/** === Configuration === */
// Wi‑Fi credentials are kept in `arduino_secrets.h` (excluded from version control).
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

/** MQTT broker defaults (override in arduino_secrets.h if needed). */
#ifndef SECRET_MQTT_HOST
#define SECRET_MQTT_HOST "test.mosquitto.org"
#endif

#ifndef SECRET_MQTT_PORT
#define SECRET_MQTT_PORT 1883
#endif

#ifndef SECRET_MQTT_BASETOPIC
#define SECRET_MQTT_BASETOPIC "iot/agriculture"
#endif

#ifndef SECRET_PUMP_HYSTERESIS
const int PUMP_HYSTERESIS = 5;
#else
const int PUMP_HYSTERESIS = SECRET_PUMP_HYSTERESIS;
#endif

/** === Time sources === */
// Use NTP client in UTC and refresh every 60 seconds
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "uk.pool.ntp.org", 0, 60000);

/** === Local display (I2C) === */
/** Support I2C LCD at address 0x27. SDA is A4 and SCL is A5 */
LiquidCrystal_I2C lcd(0x27, 16, 2);

/** === Display timing === */
unsigned long lastDisplay = 0;
const unsigned long displayInterval = 5000;

/** === DHT sensor === */
/** Digital pin D5 for DHT11 data. */
#define DHTPIN 5
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
/** Store latest sensor values updated from the DHT */
float lastTemp = NAN;
float lastHum = NAN;

/** === Water level sensor === */
/** Analogue pin A0 for water level */
#define WATER_PIN A0
/** Store water level as 0 to 100 per cent and use -1 for unknown */
int lastLevel = -1;

/** === Relay / Pump control === */
/** Digital pin D7 for the relay. Relay is active high. */
#define RELAY_PIN 7
#define RELAY_ACTIVE_HIGH 1
/** Store recent pump state for MQTT telemetry */
bool lastPumpOn = false;

/** === Pump control mode === */
/** Pump may be controlled automatically based on level, or forced on/off via MQTT. */
enum PumpMode
{
    MODE_AUTO,     ///< Automatic: use hysteresis logic based on water level
    MODE_FORCE_ON, ///< Forced on: pump controlled via MQTT command
    MODE_FORCE_OFF ///< Forced off: pump controlled via MQTT command
};
PumpMode pumpMode = MODE_AUTO;

/** === MQTT client === */
/** Network client and MQTT broker interface. */
WiFiClient netClient;
PubSubClient mqtt(netClient);

/** === Device identity === */
/** Derived from MAC address; used to construct MQTT topic hierarchy. */
char deviceId[20]; ///< Hex MAC without colons (up to 12 chars)
char topicBase[64]; ///< Base MQTT topic: `iot/agriculture/<DEVICE_ID>`

/** === Function declarations === */
/**
 * @brief Ensure WiFi is connected, reconnecting if necessary.
 */
void ensureWifi();

/**
 * @brief Ensure MQTT connection, handle subscriptions.
 */
void ensureMqtt();

/**
 * @brief MQTT message callback for control commands.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length);

/**
 * @brief Publish device status to MQTT (connected and IP address).
 */
void publishStatus(bool retained = true);

/**
 * @brief Publish sensor readings and pump state as JSON.
 */
void publishSensor(bool retained = false);

/**
 * @brief Publish pump state (on/off) to MQTT.
 */
void publishPumpState(bool retained = true);

/**
 * @brief Initialize device identity (MAC address) for MQTT topics.
 */
/**
 * Extracts the device MAC address and constructs the MQTT topic base from it.
 * Stores the hex MAC (without colons) in `deviceId` and the full topic base in `topicBase`.
 */
void initDeviceIdentity()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(topicBase, sizeof(topicBase), "%s/%s", SECRET_MQTT_BASETOPIC, deviceId);
}

/**
 * @brief Format NTP time as a local time string (HH:MM).
 *
 * Queries the NTP client for the current epoch and formats it as a simple
 * time string. Returns "--:--" if NTP sync has not yet occurred.
 */
void formatLocalTime(char *buf, size_t buflen)
{
    timeClient.update();
    unsigned long epoch = timeClient.getEpochTime();
    // Return formatted time or placeholder if NTP is not yet synchronised
    if (epoch == 0)
    {
        snprintf(buf, buflen, "--:--");
        return;
    }
    unsigned long minutes = (epoch / 60) % 60;
    unsigned long hours = (epoch / 3600) % 24;
    snprintf(buf, buflen, "%02lu:%02lu", hours, minutes);
}

// MQTT reconnect + subscriptions
void ensureMqtt()
{
    if (mqtt.connected())
        return;

    char clientId[40];
    snprintf(clientId, sizeof(clientId), "agri-%s", deviceId[0] ? deviceId : "node");

    // Last Will: status/connected = false
    char willTopic[96];
    snprintf(willTopic, sizeof(willTopic), "%s/status/connected", topicBase);

    mqtt.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    mqtt.setCallback(mqttCallback);

    bool ok = false;
#if defined(SECRET_MQTT_USER) && defined(SECRET_MQTT_PASS)
    ok = mqtt.connect(clientId, SECRET_MQTT_USER, SECRET_MQTT_PASS, willTopic, 1, true, "false");
#else
    ok = mqtt.connect(clientId, willTopic, 1, true, "false");
#endif
    if (!ok)
    {
        // Give a moment before next loop retry
        delay(500);
        return;
    }

    // Publish connection status and resubscribe to topics.
    publishStatus(true);

    char subTopic[96];
    snprintf(subTopic, sizeof(subTopic), "%s/pump/cmd", topicBase);
    mqtt.subscribe(subTopic);
}

/**
 * Checks WiFi connection status and initiates a connection if not already connected.
 * Waits for connection with a 15 second timeout.
 */
void ensureWifi()
{
    if (WiFi.status() == WL_CONNECTED)
        return;
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
    {
        delay(250);
    }
}

/**
 * Publishes device status to MQTT: connection state and IP address.
 * Both messages are retained on the broker for late subscribers.
 */
void publishStatus(bool retained)
{
    if (!mqtt.connected())
        return;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/status/connected", topicBase);
    mqtt.publish(topic, "true", retained);

    // Also publish the device's IP address as retained
    IPAddress ip = WiFi.localIP();
    char ipTopic[96];
    char ipBuf[32];
    snprintf(ipTopic, sizeof(ipTopic), "%s/status/ip", topicBase);
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    mqtt.publish(ipTopic, ipBuf, true);
}

/**
 * Publishes the current pump state (on/off) to MQTT.
 * The message is retained so subscribers always see the latest state.
 */
void publishPumpState(bool retained)
{
    if (!mqtt.connected())
        return;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/pump/state", topicBase);
    mqtt.publish(topic, lastPumpOn ? "on" : "off", retained);
}

/**
 * Publishes sensor readings and device state as a JSON payload to MQTT.
 *
 * JSON fields: temperature (float °C), humidity (percent), level (percent),
 * pump (boolean), mode (string: auto/on/off), and time (HH:MM).
 * Null values are used when sensors are unavailable.
 */
void publishSensor(bool retained)
{
    if (!mqtt.connected())
        return;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/sensor", topicBase);

    char timeStr[16];
    formatLocalTime(timeStr, sizeof(timeStr));

    // Build JSON payload with all sensor readings and pump/mode state
    char payload[192];
    const char *modeStr = (pumpMode == MODE_AUTO) ? "auto" : (pumpMode == MODE_FORCE_ON ? "on" : "off");
    // Handle nulls for missing values for temperature, humidity, and water level
    char tempBuf[12];
    char humBuf[12];
    if (isnan(lastTemp))
        strcpy(tempBuf, "null");
    else
        dtostrf(lastTemp, 0, 1, tempBuf);
    if (isnan(lastHum))
        strcpy(humBuf, "null");
    else
        dtostrf(lastHum, 0, 0, humBuf);

    if (lastLevel < 0)
    {
        snprintf(payload, sizeof(payload),
                 "{\"temperature\":%s,\"humidity\":%s,\"level\":null,\"pump\":%s,\"mode\":\"%s\",\"time\":\"%s\"}",
                 tempBuf, humBuf, lastPumpOn ? "true" : "false", modeStr, timeStr);
    }
    else
    {
        snprintf(payload, sizeof(payload),
                 "{\"temperature\":%s,\"humidity\":%s,\"level\":%d,\"pump\":%s,\"mode\":\"%s\",\"time\":\"%s\"}",
                 tempBuf, humBuf, lastLevel, lastPumpOn ? "true" : "false", modeStr, timeStr);
    }
    mqtt.publish(topic, payload, retained);
}

/**
 * @brief Handle incoming MQTT messages on subscribed topics.
 *
 * Processes pump control commands from the pump/cmd topic. Accepts commands:
 * "auto", "on", or "off" to set the pump mode. After processing, publishes
 * the updated state and sensor data back to the broker.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    // Copy and null-terminate the payload for string comparison
    char buf[16];
    unsigned int n = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';

    if (strcmp(buf, "auto") == 0)
    {
        pumpMode = MODE_AUTO;
    }
    else if (strcmp(buf, "on") == 0)
    {
        pumpMode = MODE_FORCE_ON;
    }
    else if (strcmp(buf, "off") == 0)
    {
        pumpMode = MODE_FORCE_OFF;
    }

    // Publish updated state after command
    publishPumpState(true);
    publishSensor(false);
}

/**
 * @brief Initialize hardware, sensors, WiFi, and MQTT client.
 *
 * Sets up serial communication, LCD display, DHT sensor, relay pin, WiFi connection,
 * device identity (MAC-based), NTP synchronisation, and MQTT connection with topic
 * subscriptions.
 */
void setup()
{
    Serial.begin(9600);
    // Wait for serial port to connect (3 second timeout).
    while (!Serial && millis() < 3000)
    {
        ;
    }
    Serial.println("=== Arduino R4 WiFi - MQTT Mode ===");

    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Starting...");

    dht.begin();
    pinMode(RELAY_PIN, OUTPUT);
    if (RELAY_ACTIVE_HIGH)
        digitalWrite(RELAY_PIN, LOW);
    else
        digitalWrite(RELAY_PIN, HIGH);

    ensureWifi();
    initDeviceIdentity();

    timeClient.begin();
    timeClient.update();

    ensureMqtt();
    lcd.setCursor(0, 1);
    lcd.print("MQTT ready     ");
}

/**
 * @brief Main loop: maintain WiFi and MQTT connections, read sensors, control pump, and publish telemetry.
 *
 * At a controlled interval (displayInterval), reads the DHT sensor and water level,
 * updates the pump state using hysteresis logic or forced mode, updates the LCD display,
 * and publishes sensor data and pump state to MQTT. Between intervals, maintains
 * network connectivity and handles incoming MQTT messages.
 */
void loop()
{
    ensureWifi();
    ensureMqtt();
    mqtt.loop();

    unsigned long now = millis();
    if (now - lastDisplay >= displayInterval)
    {
        lastDisplay = now;

        // Read and store DHT sensor values (temperature in °C, humidity in %)
        float h = dht.readHumidity();
        float t = dht.readTemperature();
        lastTemp = t;
        lastHum = h;

        // Read analogue water level and convert to percentage (0–100)
        int raw = analogRead(WATER_PIN);
        int level = map(raw, 0, 1023, 0, 100);
        if (level < 0)
            level = 0;
        if (level > 100)
            level = 100;
        lastLevel = level;

        // Determine pump state based on control mode
        bool pumpOn = false;
        if (pumpMode == MODE_FORCE_ON)
        {
            pumpOn = true;
        }
        else if (pumpMode == MODE_FORCE_OFF)
        {
            pumpOn = false;
        }
        else
        {
            // MODE_AUTO: use hysteresis to prevent rapid toggling
            if (lastLevel >= 0)
            {
                if (!lastPumpOn)
                {
                    // Currently off: turn on when level falls strictly below target
                    pumpOn = (lastLevel < SECRET_TARGET_LEVEL);
                }
                else
                {
                    // Currently on: stay on until level rises above target + hysteresis
                    pumpOn = (lastLevel <= (SECRET_TARGET_LEVEL + PUMP_HYSTERESIS));
                }
            }
        }
        lastPumpOn = pumpOn;

        // Drive relay according to pump state (respect RELAY_ACTIVE_HIGH).
        if (pumpOn)
        {
            if (RELAY_ACTIVE_HIGH)
                digitalWrite(RELAY_PIN, HIGH);
            else
                digitalWrite(RELAY_PIN, LOW);
        }
        else
        {
            if (RELAY_ACTIVE_HIGH)
                digitalWrite(RELAY_PIN, LOW);
            else
                digitalWrite(RELAY_PIN, HIGH);
        }

        // Prepare and print two fixed-width (16 char) LCD lines with current readings and state.
        char line1[17];
        char line2[17];
        if (isnan(h) || isnan(t))
        {
            snprintf(line1, sizeof(line1), "DHT error");
        }
        else
        {
            snprintf(line1, sizeof(line1), "T:%4.1fC H:%3.0f%%", t, h);
        }
        snprintf(line2, sizeof(line2), "Lvl:%3d%% P:%s", lastLevel >= 0 ? lastLevel : 0, pumpOn ? "On" : "Off");
        // Pad lines with spaces to exactly 16 characters for clean LCD display
        for (size_t i = strlen(line1); i < 16; ++i)
            line1[i] = ' ';
        line1[16] = '\0';
        for (size_t i = strlen(line2); i < 16; ++i)
            line2[i] = ' ';
        line2[16] = '\0';
        lcd.setCursor(0, 0);
        lcd.print(line1);
        lcd.setCursor(0, 1);
        lcd.print(line2);

        // Publish sensor readings and pump state to the MQTT broker
        publishPumpState(true);
        publishSensor(false);
    }

    delay(1);
}
