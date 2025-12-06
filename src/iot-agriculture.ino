#include <WiFiS3.h>
#include "arduino_secrets.h"

// WiFi credentials are stored in arduino_secrets.h
// arduino_secrets.h is in .gitignore to secure credentials.
char ssid[] = SECRET_SSID;
char password[] = SECRET_PASS;

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
    //main code here
}