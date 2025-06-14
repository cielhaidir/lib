#include <FitInfinityAPI.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Device configuration
const char* baseUrl = "https://your-fitinfinity-domain.com";
const char* deviceId = "your-device-id";
const char* accessKey = "your-access-key";

// Initialize components
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
FitInfinityAPI api(baseUrl, deviceId, accessKey);

const int SD_CS_PIN = 5;  // SD card CS pin

void setup() {
  Serial.begin(115200);
  Serial.println("\nFitInfinity RFID Attendance Example");
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize PN532
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    while (1); // halt
  }
  
  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Configure PN532 to read RFID tags
  nfc.SAMConfig();
  
  // Connect to WiFi and initialize API
  if (api.begin(ssid, password, SD_CS_PIN)) {
    Serial.println("Connected to WiFi and API!");
  } else {
    Serial.println("Failed to connect: " + api.getLastError());
  }
}

void loop() {
  // Regular attendance monitoring
  if (api.isConnected()) {
    // Buffer for storing RFID card ID
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    
    // Check for RFID card
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      // Convert UID to hex string
      String rfidTag = "";
      for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) {
          rfidTag += "0";
        }
        rfidTag += String(uid[i], HEX);
      }
      rfidTag.toUpperCase();
      
      // Log the RFID attendance
      Serial.print("Found RFID card: "); Serial.println(rfidTag);
      if (api.logRFID(rfidTag.c_str())) {
        Serial.println("Attendance logged successfully!");
      } else {
        Serial.println("Failed to log attendance: " + api.getLastError());
      }
      
      delay(2000); // Prevent multiple scans of the same card
    }
    
    // Sync any offline records
    api.syncOfflineRecords();
  }
  
  delay(100); // Small delay to prevent tight looping
}