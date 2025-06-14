#include <FitInfinityAPI.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// API configuration
const char* apiUrl = "https://your-api-domain.com/api/trpc/esp32";
const char* deviceId = "your_device_id";
const char* accessKey = "your_access_key";

// Initialize API client
FitInfinityAPI api(apiUrl, deviceId, accessKey);

// Example RFID card number
const char* testRFID = "1234567890";

void setup() {
    Serial.begin(115200);
    Serial.println("Starting Attendance System...");

    // Connect to WiFi and initialize the API
    if (api.begin(ssid, password)) {
        Serial.println("Connected and authenticated successfully!");
    } else {
        Serial.println("Connection failed: " + api.getLastError());
    }
}

void loop() {
    // Simulate fingerprint detection
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        int fingerId = input.toInt();
        
        if (api.logFingerprint(fingerId)) {
            Serial.println("Fingerprint attendance logged successfully!");
        } else {
            Serial.println("Failed to log fingerprint: " + api.getLastError());
        }
    }

    // If connection was lost, try to sync offline records
    if (api.isConnected()) {
        if (api.syncOfflineRecords()) {
            Serial.println("Offline records synced successfully!");
        }
    }

    delay(1000);
}