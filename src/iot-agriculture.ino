#include <WiFiS3.h>
#include "arduino_secrets.h"

// WiFi credentials are stored in arduino_secrets.h
// arduino_secrets.h is in .gitignore to secure credentials.
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

// Simple HTTP server on port 80
WiFiServer server(80);
// Mobile-friendly Bootstrap landing page is stored in a separate header
#include "index_page.h"

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

    // Verify Integrity of WiDI Module on Board
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
    } else {
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