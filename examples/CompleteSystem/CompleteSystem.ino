#include <FitInfinityAPI.h>
#include <SoftwareSerial.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Device configuration
const char* baseUrl = "https://your-fitinfinity-domain.com";
const char* deviceId = "your-device-id";
const char* accessKey = "your-access-key";

// Fingerprint sensor pins
const int FINGER_RX = 16;  // GPIO16 -> sensor TX
const int FINGER_TX = 17;  // GPIO17 -> sensor RX
const int SD_CS_PIN = 5;   // SD card CS pin

// Initialize components
HardwareSerial fingerSerial(2);
FitInfinityAPI api(baseUrl, deviceId, accessKey);

void setup() {
  Serial.begin(115200);
  Serial.println("\nFitInfinity Complete System Example");
  
fingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  // Connect to WiFi and initialize API
  if (api.begin(ssid, password, SD_CS_PIN)) {
    Serial.println("Connected to WiFi and API!");
  } else {
    Serial.println("Failed to connect: " + api.getLastError());
  }
  
  // Initialize fingerprint sensor
  if (api.beginFingerprint(&fingerSerial)) {
    Serial.println("Fingerprint sensor initialized");
  } else {
    Serial.println("Fingerprint sensor error: " + api.getLastError());
  }
}

void loop() {
  // Check for pending enrollments
  DynamicJsonDocument doc(1024);
  JsonArray enrollments = doc.to<JsonArray>();
  
  if (api.getPendingEnrollments(enrollments)) {
    for (JsonObject enrollment : enrollments) {
      const char* id = enrollment["id"];
      const char* nama = enrollment["nama"];
      int fingerId = enrollment["finger_id"].as<int>();
      
      // Show enrollment instructions
      Serial.println("\nNew enrollment request:");
      Serial.printf("Name: %s (ID: %s)\n", nama, id);
      Serial.println("Place finger on sensor...");
      
      // Perform enrollment
      bool success = api.enrollFingerprint(fingerId);
      if (success) {
        Serial.printf("Successfully enrolled fingerprint for %s\n", nama);
        api.updateEnrollmentStatus(id, fingerId, true);
      } else {
        Serial.printf("Failed to enroll: %s\n", api.getLastError());
        api.updateEnrollmentStatus(id, fingerId, false);
      }
    }
  }
  
  // Regular attendance monitoring
  if (api.isConnected()) {
    int fingerprintId = -1;  // Store detected fingerprint ID
    uint8_t result = api.scanFingerprint(&fingerprintId);
    
    if (result == FINGERPRINT_OK && fingerprintId >= 0) {
      if (api.logFingerprint(fingerprintId)) {
        Serial.println("Attendance logged successfully!");
      } else {
        Serial.println("Failed to log attendance: " + api.getLastError());
      }
      delay(2000);  // Prevent multiple scans
    }
    
    // Sync any offline records
    api.syncOfflineRecords();
  }
  
  delay(100);  // Small delay to prevent tight looping
}