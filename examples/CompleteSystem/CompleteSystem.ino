#include <FitInfinityAPI.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Device configuration
const char* baseUrl = "https://your-fitinfinity-domain.com";
const char* deviceId = "your-device-id";
const char* accessKey = "your-access-key";

// Pin definitions
const int FINGER_RX = 16;  // GPIO16 -> sensor TX
const int FINGER_TX = 17;  // GPIO17 -> sensor RX
const int SD_CS_PIN = 5;   // SD card CS pin
const int BUZZER_PIN = 4;  // Buzzer pin

// LCD configuration
const int lcdCols = 16;
const int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdCols, lcdRows);

// Initialize components
HardwareSerial fingerSerial(2);
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
FitInfinityAPI api(baseUrl, deviceId, accessKey);

void soundSuccess() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

void soundError() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nFitInfinity Complete System Example");
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FitInfinity");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize I2C and RFID
  Wire.begin();
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board");
    lcd.clear();
    lcd.print("RFID Error!");
    while (1); // halt
  }
  
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Configure PN532 to read RFID tags
  nfc.SAMConfig();
  
  fingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  // Connect to WiFi and initialize API
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  if (api.begin(ssid, password, SD_CS_PIN)) {
    Serial.println("Connected to WiFi and API!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    delay(1000);
  } else {
    Serial.println("Failed to connect: " + api.getLastError());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    delay(2000);
  }
  
  // Initialize fingerprint sensor
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Init Finger...");
  
  if (api.beginFingerprint(&fingerSerial)) {
    Serial.println("Fingerprint sensor initialized");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Finger Ready!");
    delay(1000);
  } else {
    Serial.println("Fingerprint sensor error: " + api.getLastError());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Finger Error!");
    delay(2000);
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
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("New Enrollment:");
      lcd.setCursor(0, 1);
      lcd.print(nama);
      delay(2000);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Place finger");
      lcd.setCursor(0, 1);
      lcd.print("on sensor...");
      
      // Perform enrollment
      bool success = api.enrollFingerprint(fingerId);
      if (success) {
        Serial.printf("Successfully enrolled fingerprint for %s\n", nama);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Enrollment OK!");
        soundSuccess();
        api.updateEnrollmentStatus(id, fingerId, true);
      } else {
        Serial.printf("Failed to enroll: %s\n", api.getLastError());
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Enrollment");
        lcd.setCursor(0, 1);
        lcd.print("Failed!");
        soundError();
        api.updateEnrollmentStatus(id, fingerId, false);
      }
      delay(2000);
    }
  }
  
  // Regular attendance monitoring
  if (api.isConnected()) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready to Scan");
    
    // Check for fingerprint
    int fingerprintId = -1;
    uint8_t result = api.scanFingerprint(&fingerprintId);
    
    if (result == FINGERPRINT_OK && fingerprintId >= 0) {
      if (api.logFingerprint(fingerprintId)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fingerprint");
        lcd.setCursor(0, 1);
        lcd.print("Success! ID:");
        lcd.print(fingerprintId);
        soundSuccess();
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fingerprint");
        lcd.setCursor(0, 1);
        lcd.print("Failed!");
        soundError();
      }
      delay(2000);
    }
    
    // Check for RFID card
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      String rfidTag = "";
      for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) {
          rfidTag += "0";
        }
        rfidTag += String(uid[i], HEX);
      }
      rfidTag.toUpperCase();
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Card Found:");
      lcd.setCursor(0, 1);
      lcd.print(rfidTag);
      
      if (api.logRFID(rfidTag.c_str())) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("RFID Success!");
        soundSuccess();
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("RFID Failed!");
        soundError();
      }
      delay(2000);
    }
    
    // Sync any offline records
    api.syncOfflineRecords();
  }
  
  delay(100);  // Small delay to prevent tight looping
}