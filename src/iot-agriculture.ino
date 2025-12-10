#include <WiFiS3.h>
#include "arduino_secrets.h"
// Use NTP instead of the RTC library to save RAM
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
// Camera (ArduCAM Mini OV2640)
// Using R4-compatible fork: https://github.com/keeeal/ArduCAM-Arduino-Uno-R4
#include <SPI.h>
#include "memorysaver.h"
#include <ArduCAM.h>

// === Configuration ===
// Wi‑Fi credentials are kept in `arduino_secrets.h` (excluded from version control).
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

// === HTTP server ===
// Serve a simple dashboard and status endpoints on HTTP port 80
WiFiServer server(80);
// Provide dashboard HTML via `index_page.h`
#include "index_page.h"

// === Time sources ===
// Use NTP as the primary time source
bool rtcPresent = false;

// Use NTP client in UTC and refresh every 60 seconds
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "uk.pool.ntp.org", 0, 60000);

// === Local display (I2C) ===
// Support I2C LCD at address 0x27. SDA is A4 and SCL is A5
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === ArduCAM configuration ===
// SPI pins: SCK=D13 MISO=D12 MOSI=D11 CS=D10. Share I2C with LCD for SDA/SCL.
#define CAM_CS_PIN 10
ArduCAM myCAM(OV2640, CAM_CS_PIN);

// Toggle camera functionality at runtime
// When false, skip camera initialisation and return 503 for `/image` requests
bool cameraEnabled = false;
// Track whether a camera responds on the SPI bus (ie showing it is physically present)
bool cameraPresent = false;

// === DHT sensor ===
// Use DHT11 on a digital pin. Change `DHTPIN` if required
#define DHTPIN 5
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Display update timing in milliseconds
unsigned long lastDisplay = 0;
const unsigned long displayInterval = 5000;

// Store latest sensor values updated from the DHT
float lastTemp = NAN;
float lastHum = NAN;
// Analogue pin A0 for water level
#define WATER_PIN A0
// Store water level as 0 to 100 per cent and use -1 for unknown
int lastLevel = -1;
// Store recent pump state for the web UI
bool lastPumpOn = false;

// Set a small per-chunk buffer for camera streaming
const size_t CAM_CHUNK = 64;
// Limit streamed bytes per capture to avoid long transfers and memory pressure (unsuccessful)
const uint32_t MAX_STREAM_BYTES = 2048; // 2 KB

// Digital pin D7 for the relay. Relay is active high
#define RELAY_PIN 7
#define RELAY_ACTIVE_HIGH 1

// === Time helpers (epoch to date conversion without RTClib) ===
// Converts epoch seconds to year, month, day, hour, minute, second.
struct DateTimeStruct {
    int year, month, day, hour, minute, second;
};

DateTimeStruct epochToDateTime(unsigned long epoch) {
    DateTimeStruct dt;
    dt.second = epoch % 60;
    epoch /= 60;
    dt.minute = epoch % 60;
    epoch /= 60;
    dt.hour = epoch % 24;
    epoch /= 24;

    // Days since 1970-01-01.
    unsigned long days = epoch;
    dt.year = 1970;

    // Leap year helper.
    auto isLeapYear = [](int y) { return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)); };

    // Count forward to find year.
    while (true) {
        int daysInYear = isLeapYear(dt.year) ? 366 : 365;
        if (days < daysInYear) break;
        days -= daysInYear;
        dt.year++;
    }

    // Days in each month.
    int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (isLeapYear(dt.year)) daysInMonth[2] = 29;

    // Count forward to find month.
    dt.month = 1;
    while (days >= daysInMonth[dt.month]) {
        days -= daysInMonth[dt.month];
        dt.month++;
    }

    dt.day = days + 1;
    return dt;
}

// Determines whether a UTC epoch is within BST (simplified: Mar 25 - Oct 25).
bool isBST(unsigned long utcEpoch) {
    DateTimeStruct dt = epochToDateTime(utcEpoch);
    // BST runs roughly from last Sunday of March to last Sunday of October.
    // For simplicity, using March 25 to October 25.
    bool inRange = (dt.month > 3 || (dt.month == 3 && dt.day >= 25)) &&
                   (dt.month < 10 || (dt.month == 10 && dt.day <= 25));
    return inRange;
}

// Converts a UTC epoch to a local UK epoch by applying a +1 hour offset when BST applies.
unsigned long ukLocalEpoch(unsigned long utcEpoch) {
    if (isBST(utcEpoch)) return utcEpoch + 3600;
    return utcEpoch;
}

void setup()
{
    Serial.begin(9600);
    // Wait for serial port to connect (3 second timeout).
    while (!Serial && millis() < 3000)
    {
        ;
    }

    Serial.println("=== Arduino R4 WiFi - Starting ===");
    Serial.println("Serial communication initialised");
    delay(1000);

    // Initialise I2C and LCD for hardware test.
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello world");

    // Initialise SPI and test the ArduCAM SPI bus so the device can report
    // whether a camera is present even when image capture is disabled
    pinMode(CAM_CS_PIN, OUTPUT);
    digitalWrite(CAM_CS_PIN, HIGH);
    SPI.begin();
    delay(100);
    myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
    if (myCAM.read_reg(ARDUCHIP_TEST1) != 0x55) {
        Serial.println("ArduCAM SPI failure - camera may not be present");
        cameraPresent = false;
    } else {
        Serial.println("ArduCAM detected");
        cameraPresent = true;
    }

    // Perform full camera initialisation only when the camera is enabled
    // and a module responded to the SPI test
    if (cameraEnabled && cameraPresent) {
        myCAM.set_format(JPEG);
        myCAM.InitCAM();
        // Set a smaller JPEG resolution to reduce frame size (160x120)
        myCAM.OV2640_set_JPEG_size(OV2640_160x120);
    } else if (!cameraEnabled) {
        Serial.println("Camera functionality disabled (cameraEnabled=false)");
    } else if (!cameraPresent) {
        Serial.println("Camera not present, skipping initialisation");
    }

    // Initialise DHT sensor.
    dht.begin();

    // Initialise relay pin for pump control.
    pinMode(RELAY_PIN, OUTPUT);
    // Ensure pump is off initially.
    if (RELAY_ACTIVE_HIGH) digitalWrite(RELAY_PIN, LOW); else digitalWrite(RELAY_PIN, HIGH);

    // Verify WiFi module presence.
    Serial.println("Checking for WiFi module...");
    if (WiFi.status() == WL_NO_MODULE)
    {
        Serial.println("ERROR: Communication with WiFi module failed!");
        while (true)
            ;
    }
    Serial.println("WiFi module detected successfully");

    // Connect to WiFi (10 second timeout).
    Serial.print("Connecting to WiFi network: ");
    Serial.println(ssid);

    int status = WL_IDLE_STATUS;
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000;

    while (status != WL_CONNECTED && millis() - startAttemptTime < timeout)
    {
        status = WiFi.begin(ssid, password);
        delay(500);
        Serial.print(".");
    }

    if (status == WL_CONNECTED)
    {
        Serial.println("\nConnected to WiFi");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Signal strength (RSSI): ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        // Start HTTP server.
        server.begin();
        Serial.print("HTTP server started. Open http://");
        Serial.print(WiFi.localIP());
        Serial.println(" on your phone or computer.");
        // Start NTP client and attempt initial sync.
        timeClient.begin();
        timeClient.update();
        unsigned long epoch = timeClient.getEpochTime();
        if (epoch != 0) {
            Serial.print("NTP time: ");
            Serial.println(epoch);
        } else {
            Serial.println("NTP sync failed");
        }
    }
    else
    {
        Serial.println("\nFailed to connect to WiFi");
        Serial.println("Connection timeout - please check:");
        Serial.println("  - WiFi credentials are correct");
        Serial.println("  - WiFi network is in range");
        Serial.println("  - WiFi network is operational");
    }
}

void loop()
{
    // Update LCD with sensor data at a controlled interval.
    unsigned long now = millis();
    if (now - lastDisplay >= displayInterval) {
        lastDisplay = now;

        float h = dht.readHumidity();
        float t = dht.readTemperature(); // Celsius

        // Update stored sensor values so the web endpoint can report them.
        lastTemp = t;
        lastHum = h;

        // Read water level on analog pin and convert to percentage.
        int raw = analogRead(WATER_PIN);
        int level = map(raw, 0, 1023, 0, 100);
        if (level < 0) level = 0; if (level > 100) level = 100;
        lastLevel = level;

        lcd.clear();
        if (isnan(h) || isnan(t)) {
            lcd.setCursor(0, 0);
            lcd.print("DHT error");
        } else {
            lcd.setCursor(0, 0);
            lcd.print("T:");
            lcd.print(t, 1);
            lcd.print("C ");
            lcd.print("H:");
            lcd.print(h, 0);
            lcd.print("%");
        }

        // Determine pump state by comparing level to target.
        bool pumpOn = false;
        if (lastLevel >= 0) {
            pumpOn = (lastLevel < SECRET_TARGET_LEVEL);
        }
        // Publish the last pump state so the web endpoint can report it.
        lastPumpOn = pumpOn;

        // Drive relay according to pump state (respect RELAY_ACTIVE_HIGH).
        if (pumpOn) {
            if (RELAY_ACTIVE_HIGH) digitalWrite(RELAY_PIN, HIGH); else digitalWrite(RELAY_PIN, LOW);
        } else {
            if (RELAY_ACTIVE_HIGH) digitalWrite(RELAY_PIN, LOW); else digitalWrite(RELAY_PIN, HIGH);
        }

        // Second line: show water level percentage and pump state (short form).
        lcd.setCursor(0, 1);
        char lvlBuf[17];
        snprintf(lvlBuf, sizeof(lvlBuf), "Lvl:%3d%% P:%s", lastLevel >= 0 ? lastLevel : 0, pumpOn ? "On" : "Off");
        lcd.print(lvlBuf);
    }

    // Handle incoming HTTP clients.
    WiFiClient client = server.available();
    if (!client) {
        delay(10);
        return;
    }

    Serial.println("New HTTP client");

    // Read request (2 second timeout).
    String request = "";
    unsigned long start = millis();
    while (client.connected() && (millis() - start) < 2000) {
        while (client.available()) {
            char c = client.read();
            request += c;
            if (request.endsWith("\r\n\r\n")) break;
        }
        if (request.endsWith("\r\n\r\n")) break;
    }

    Serial.println("Request:");
    Serial.println(request);

    // Simple request routing.
    // Image endpoint: perform an on-demand capture and attempt to stream the JPEG
    // directly to the client in small chunks to avoid large RAM buffer.
    if (request.indexOf("GET /image") >= 0) {
        if (!cameraEnabled) {
            client.println("HTTP/1.1 503 Service Unavailable");
            client.println("Content-Type: text/plain; charset=utf-8");
            client.println("Connection: close");
            client.println();
            client.println("Camera disabled on device");
        } else {
            // Start capture.
            myCAM.flush_fifo();
            myCAM.clear_fifo_flag();
            myCAM.start_capture();
            unsigned long startCap = millis();
            while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
                if (millis() - startCap > 2000) break;
            }

            uint32_t length = myCAM.read_fifo_length();
            if (length == 0) {
                // No image captured — return a 204-like empty response.
                client.println("HTTP/1.1 204 No Content");
                client.println("Connection: close");
                client.println();
            } else if (length > MAX_STREAM_BYTES) {
                // Too large to safely stream on this device with current memory
                // settings. Discard FIFO and notify client.
                myCAM.clear_fifo_flag();
                Serial.print("Captured JPEG too large: ");
                Serial.print(length);
                Serial.println(" bytes — rejecting to save RAM");
                client.println("HTTP/1.1 413 Payload Too Large");
                client.println("Content-Type: text/plain; charset=utf-8");
                client.println("Connection: close");
                client.println();
                client.println("Image too large for device (increase limit or lower camera resolution)");
            } else {
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: image/jpeg");
                client.print("Content-Length: ");
                client.println(length);
                client.println("Connection: close");
                client.println();

                // Stream FIFO directly to client in chunks.
                myCAM.CS_LOW();
                myCAM.set_fifo_burst();
                uint8_t chunkBuf[CAM_CHUNK];
                for (uint32_t sent = 0; sent < length; sent += CAM_CHUNK) {
                    uint32_t chunk = (length - sent > CAM_CHUNK) ? CAM_CHUNK : (length - sent);
                    for (uint32_t i = 0; i < chunk; i++) {
                        chunkBuf[i] = SPI.transfer(0x00);
                    }
                    client.write(chunkBuf, chunk);
                }
                myCAM.CS_HIGH();
                myCAM.clear_fifo_flag();
                Serial.print("Streamed JPEG: ");
                Serial.print(length);
                Serial.println(" bytes");
            }
        }
    }

    if (request.indexOf("GET /status") >= 0) {
        // Return JSON status object
        String ip = WiFi.localIP().toString();
        bool connected = (WiFi.status() == WL_CONNECTED);

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.print("{\"connected\":");
        client.print(connected ? "true" : "false");
        client.print(",\"ip\":\"");
        if (connected) client.print(ip);
        client.print("\"}");
    }
    // Sensor endpoint returns latest DHT readings.
    else if (request.indexOf("GET /sensor") >= 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.print("{\"temperature\":");
        if (!isnan(lastTemp)) client.print(lastTemp, 1); else client.print("null");
        client.print(",\"humidity\":");
        if (!isnan(lastHum)) client.print(lastHum, 0); else client.print("null");
        client.print(",\"level\":");
        if (lastLevel >= 0) client.print(lastLevel); else client.print("null");
        client.print(",\"pump\":");
        if (lastLevel >= 0) client.print(lastPumpOn ? "true" : "false"); else client.print("null");
        client.print("}");
    }
    else if (request.indexOf("GET /time") >= 0) {
        // Update NTP client and get UTC epoch.
        timeClient.update();
        unsigned long nowEpoch = timeClient.getEpochTime();

        // Compute UK-local epoch (apply BST when needed).
        unsigned long local = ukLocalEpoch(nowEpoch);

        // Format datetime as DD/MM/YYYY HH:MM (24-hour).
        DateTimeStruct localDT = epochToDateTime(local);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u/%02u/%04u %02u:%02u",
            localDT.day, localDT.month, localDT.year, localDT.hour, localDT.minute);

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.print("{\"datetime\":\"");
        client.print(buf);
        client.print("\"}");
    }
    else {
        // Serve index page (dashboard HTML).
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println(INDEX_PAGE);
    }

    delay(1);
    client.stop();
}