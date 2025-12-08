#include <WiFiS3.h>
#include "arduino_secrets.h"
#include <RTClib.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
// I2C LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials are stored in arduino_secrets.h
// arduino_secrets.h is in .gitignore to secure credentials.
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

//HTTP server on port 80
WiFiServer server(80);
// Bootstrap Dashboard is stored in a separate header
#include "index_page.h"

// RTC (using RTClib)
RTC_DS3231 rtc;
bool rtcPresent = false;

// NTP client (uses NTPClient library)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "uk.pool.ntp.org", 0, 60000); // UTC, refresh every 60s

// I2C LCD (common I2C address 0x27; change if your module uses 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Days in month helper
int daysInMonth(int year, int month) {
    if (month == 2) {
        // leap year
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        return leap ? 29 : 28;
    }
    const int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    return mdays[month];
}

// Compute whether given UTC epoch falls in British Summer Time (BST)
bool isBST(unsigned long utcEpoch) {
    DateTime dt(utcEpoch);
    int year = dt.year();

    // find last Sunday in March
    int lastDayMar = daysInMonth(year, 3);
    DateTime lastMar(year, 3, lastDayMar, 0, 0, 0);
    int wd = (lastMar.unixtime() / 86400 + 4) % 7; // 0 = Sunday
    int lastSundayMar = lastDayMar - wd;
    unsigned long bstStart = DateTime(year, 3, lastSundayMar, 1, 0, 0).unixtime(); // 01:00 UTC

    // find last Sunday in October
    int lastDayOct = daysInMonth(year, 10);
    DateTime lastOct(year, 10, lastDayOct, 0, 0, 0);
    wd = (lastOct.unixtime() / 86400 + 4) % 7;
    int lastSundayOct = lastDayOct - wd;
    unsigned long bstEnd = DateTime(year, 10, lastSundayOct, 1, 0, 0).unixtime(); // 01:00 UTC

    return (utcEpoch >= bstStart && utcEpoch < bstEnd);
}

// Return local UK epoch (applying BST if needed)
unsigned long ukLocalEpoch(unsigned long utcEpoch) {
    if (isBST(utcEpoch)) return utcEpoch + 3600;
    return utcEpoch;
}

void setup()
{
    Serial.begin(9600);
    while (!Serial && millis() < 3000)
    {
        ; // Wait for serial port to connect, with 3 second timeout
    }

    Serial.println("=== Arduino R4 WiFi - Starting ===");
    Serial.println("Serial communication initialized");
    delay(1000);

    // Initialize I2C and LCD for quick hardware test
    Wire.begin(); // SDA -> A4, SCL -> A5 on Uno/R4
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello world");

    // Initialize RTC
    Serial.println("Initializing RTC...");
    if (!rtc.begin()) {
        Serial.println("RTC not found - continuing without hardware RTC");
        rtcPresent = false;
    } else {
        rtcPresent = true;
        Serial.println("RTC initialized");
        if (rtc.lostPower()) {
            Serial.println("RTC lost power; consider setting it or allow NTP to initialise it");

        }
    }

    // Verify Integrity of WiFi Module on Board
    Serial.println("Checking for WiFi module...");
    if (WiFi.status() == WL_NO_MODULE)
    {
        Serial.println("ERROR: Communication with WiFi module failed!");
        while (true)
            ;
    }
    Serial.println("WiFi module detected successfully");

    // Connect to WiFi
    Serial.print("Connecting to WiFi network: ");
    Serial.println(ssid);

    int status = WL_IDLE_STATUS;
    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 10000; // 10 second timeout

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
        // Start HTTP server
        server.begin();
        Serial.print("HTTP server started. Open http://");
        Serial.print(WiFi.localIP());
        Serial.println(" on your phone or computer.");
        // Start NTP client and attempt initial sync
        timeClient.begin();
        timeClient.update();
        unsigned long epoch = timeClient.getEpochTime();
        if (epoch != 0) {
            Serial.print("NTP time: ");
            Serial.println(epoch);
            if (rtcPresent) {
                rtc.adjust(DateTime(epoch));
                Serial.println("RTC updated from NTP");
            }
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
    // Handle incoming HTTP clients
    WiFiClient client = server.available();
    if (!client) {
        delay(10);
        return;
    }

    Serial.println("New HTTP client");

    // Read request (with short timeout)
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

    // Simple routing
    if (request.indexOf("GET /status") >= 0) {
        // Return JSON status
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
    else if (request.indexOf("GET /time") >= 0) {
        // Update NTP client and get current UTC epoch
        timeClient.update();
        unsigned long nowEpoch = timeClient.getEpochTime();

        // fallback to RTC if NTP not available
        if (nowEpoch == 0 && rtcPresent) {
            nowEpoch = rtc.now().unixtime();
        }

        // Compute UK local time (apply BST if needed)
        unsigned long local = ukLocalEpoch(nowEpoch);
        DateTime localDT(local);

        // DD/MM/YYYY HH:MM (24-hour)
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u/%02u/%04u %02u:%02u",
                 localDT.day(), localDT.month(), localDT.year(), localDT.hour(), localDT.minute());

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.print("{\"datetime\":\"");
        client.print(buf);
        client.print("\"}");
    }
    else {
        // Serve index page
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.println(INDEX_PAGE);
    }

    delay(1);
    client.stop();
}