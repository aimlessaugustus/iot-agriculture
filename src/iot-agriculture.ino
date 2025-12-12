#include <WiFiS3.h>
#include "arduino_secrets.h"
// Use NTP instead of the RTC library for time
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
// Camera (ArduCAM Mini OV2640)
#define OV2640_MINI_2MP
// Using R4-compatible fork: https://github.com/keeeal/ArduCAM-Arduino-Uno-R4
#include <SPI.h>
#include "memorysaver.h"
#include <ArduCAM.h>

// === Configuration ===
// Wi‑Fi credentials are kept in `arduino_secrets.h` (excluded from version control).
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

// === HTTP server ===
// Use the Uno R4 webserver library for routes and authentication
#include <UnoR4WiFi_WebServer.h>
UnoR4WiFi_WebServer server;
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
// Record whether a camera was detected during initialisation
bool cameraDetectedAtInit = false;

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
const uint32_t MAX_STREAM_BYTES = 32768; // 32 KB safety bound

// We stream images on-demand in small chunks to avoid allocating large
// global buffers that can overflow the device RAM. The per-chunk buffer
// below is intentionally small (64 bytes) so the code stays within RAM limits.
// Note: the previous implementation used a 16 KB static cache which can
// exceed available RAM on the Arduino.

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

// --- Route handlers for UnoR4WiFi_WebServer ------------------------------
// Handler signature: (WiFiClient& client, const String& method, const String& request,
//                     const QueryParams& params, const String& jsonData)

void handleRoot(WiFiClient& client, const String& method, const String& request, const QueryParams& params, const String& jsonData) {
    // INDEX_PAGE is a raw HTML string defined in `index_page.h`
    server.sendResponse(client, INDEX_PAGE);
}

void handleStatus(WiFiClient& client, const String& method, const String& request, const QueryParams& params, const String& jsonData) {
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
    client.print("\",\"cameraDetected\":");
    client.print(cameraDetectedAtInit ? "true" : "false");
    client.print("}");
}

void handleSensor(WiFiClient& client, const String& method, const String& request, const QueryParams& params, const String& jsonData) {
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

void handleTime(WiFiClient& client, const String& method, const String& request, const QueryParams& params, const String& jsonData) {
    timeClient.update();
    unsigned long nowEpoch = timeClient.getEpochTime();
    unsigned long local = ukLocalEpoch(nowEpoch);
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

void handleImage(WiFiClient& client, const String& method, const String& request, const QueryParams& params, const String& jsonData) {
    if (!cameraEnabled) {
        client.println("HTTP/1.1 503 Service Unavailable");
        client.println("Content-Type: text/plain; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println("Camera disabled on device");
        return;
    }

    // Perform a fresh capture and stream the JPEG directly to the client
    // using a small temporary buffer to avoid large RAM use.
    // Start capture
    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();

    unsigned long t0 = millis();
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
        if (millis() - t0 > 2000) break;
    }

    uint32_t len = myCAM.read_fifo_length();
        if (len == 0) {
        myCAM.clear_fifo_flag();
        client.println("HTTP/1.1 204 No Content");
        client.println("Connection: close");
        client.println();
        return;
    }
    if (len >= MAX_STREAM_BYTES) {
        // If the frame is too large, log and return a 413 so
        // the client can tell the difference between empty and oversized.
        myCAM.clear_fifo_flag();
        Serial.println("handleImage: captured frame too large, rejecting");
        client.println("HTTP/1.1 413 Payload Too Large");
        client.println("Content-Type: text/plain; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println("Captured image too large");
        return;
    }

    // Send headers now that we know the content length
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.print("Content-Length: "); client.println(len);
    client.println("Connection: close");
    client.println();

    // Stream the FIFO directly in small chunks
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    const size_t BUF_SZ = CAM_CHUNK; // small stack buffer
    uint8_t buf[BUF_SZ];
    uint32_t remaining = len;
    uint32_t streamed = 0;
    // Read and send in chunks
    while (remaining) {
        size_t toRead = (remaining > BUF_SZ) ? BUF_SZ : remaining;
        for (size_t i = 0; i < toRead; ++i) {
            buf[i] = SPI.transfer(0x00);
        }
        client.write(buf, toRead);
        remaining -= toRead;
        streamed += toRead;
        // Small delay to yield to other tasks and the client
        delayMicroseconds(50);
    }
    myCAM.CS_HIGH();
    myCAM.clear_fifo_flag();
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
        cameraDetectedAtInit = false;
    } else {
        Serial.println("ArduCAM detected");
        cameraDetectedAtInit = true;
    }

    // Perform camera initialisation when a module responded to the SPI test.
    // Initialise the sensor and set a modest
    // JPEG resolution (320x240) so captured frames are reasonably sized.
    if (cameraDetectedAtInit) {
        myCAM.set_format(JPEG);
        myCAM.InitCAM();
        // Default to a small preview size for periodic dashboard captures
        myCAM.OV2640_set_JPEG_size(OV2640_320x240);
        // Enable camera functionality when initialisation succeeds.
        cameraEnabled = true;
        Serial.println("ArduCAM initialised (JPEG 320x240)");
    } else {
        Serial.println("Camera not present, skipping initialisation");
    }

    // Initialise DHT sensor.
    dht.begin();

    // Initialise relay pin for pump control.
    pinMode(RELAY_PIN, OUTPUT);
    // Ensure pump is off initially.
    if (RELAY_ACTIVE_HIGH) digitalWrite(RELAY_PIN, LOW); else digitalWrite(RELAY_PIN, HIGH);

    // Configure routes and start the UnoR4WiFi_WebServer which will handle
    // the WiFi connection and incoming HTTP clients for us.
    server.addRoute("/", handleRoot);
    server.addRoute("/status", handleStatus);
    server.addRoute("/sensor", handleSensor);
    server.addRoute("/time", handleTime);
    server.addRoute("/image", handleImage);

    // Enable simple Basic Auth — credentials must be provided in `arduino_secrets.h`
    server.enableAuthentication(SECRET_BASIC_USER, SECRET_BASIC_PASS, "Smart Agriculture");

    // Start the server and let it manage the WiFi connection.
    server.begin(ssid, password);
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

    // Let the UnoR4WiFi_WebServer handle incoming HTTP requests and routing.
    server.handleClient();
    delay(1);
}
