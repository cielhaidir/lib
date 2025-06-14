#include <FitInfinityAPI.h>

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// API configuration
const char* apiUrl = "https://your-api-domain.com/api/trpc/esp32";
const char* deviceId = "your_device_id";
const char* accessKey = "your_access_key";

// SD Card configuration
const int sdChipSelect = 5;  // SD card CS pin (change as needed)

// Initialize API client
FitInfinityAPI api(apiUrl, deviceId, accessKey);

void setup() {
    Serial.begin(115200);
    Serial.println("Starting Attendance System with SD Card support...");

    // Connect to WiFi and initialize the API with SD card
    if (api.begin(ssid, password, sdChipSelect)) {
        Serial.println("Connected and authenticated successfully!");
        if (api.isSDCardEnabled()) {
            Serial.println("SD Card initialized for offline storage");
            Serial.println(api.getOfflineStorageStats());
        } else {
            Serial.println("Using memory-based offline storage");
        }
    } else {
        Serial.println("Connection failed: " + api.getLastError());
    }
}

void loop() {
    // Example fingerprint detection
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        if (input.startsWith("F")) {  // Fingerprint
            int fingerId = input.substring(1).toInt();
            handleFingerprint(fingerId);
        } else if (input.startsWith("R")) {  // RFID
            String rfid = input.substring(1);
            handleRFID(rfid.c_str());
        } else if (input == "SYNC") {
            syncOfflineRecords();
        } else if (input == "STATS") {
            showStorageStats();
        }
    }

    delay(100);
}

void handleFingerprint(int fingerId) {
    Serial.print("Processing fingerprint ID: ");
    Serial.println(fingerId);
    
    if (api.logFingerprint(fingerId)) {
        Serial.println("Fingerprint attendance logged successfully!");
    } else {
        Serial.println("Failed to log fingerprint: " + api.getLastError());
        Serial.println("Record stored offline for later sync");
    }
}

void handleRFID(const char* rfidNumber) {
    Serial.print("Processing RFID: ");
    Serial.println(rfidNumber);
    
    if (api.logRFID(rfidNumber)) {
        Serial.println("RFID attendance logged successfully!");
    } else {
        Serial.println("Failed to log RFID: " + api.getLastError());
        Serial.println("Record stored offline for later sync");
    }
}

void syncOfflineRecords() {
    Serial.println("Syncing offline records...");
    if (api.syncOfflineRecords()) {
        Serial.println("Offline records synced successfully!");
        Serial.println(api.getOfflineStorageStats());
    } else {
        Serial.println("Failed to sync some records: " + api.getLastError());
    }
}

void showStorageStats() {
    Serial.println("\n=== Storage Stats ===");
    Serial.println(api.getOfflineStorageStats());
    Serial.println("===================\n");
}