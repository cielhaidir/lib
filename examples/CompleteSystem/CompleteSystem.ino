#include <FitInfinityAPI.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Device configuration
const char* baseUrl = "https://your-fitinfinity-domain.com";
const char* deviceId = "your-device-id";
const char* accessKey = "your-access-key";

// Pin definitions
const int FINGER_RX = 19;  // GPIO19 -> sensor TX
const int FINGER_TX = 18;  // GPIO18 -> sensor RX

const int DF_RX = 16;      // DFPlayer RX pin
const int DF_TX = 17;      // DFPlayer TX pin

// const int SD_CS_PIN = 5;   // SD card CS pin (commented out as not used)
const int BUZZER_PIN = 4;  // Buzzer pin

// LCD configuration
const int lcdCols = 16;
const int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdCols, lcdRows);

// DFPlayer Mini setup
HardwareSerial dfSerial(1); // UART1
DFRobotDFPlayerMini dfplayer;

// Initialize components
HardwareSerial fingerSerial(2);
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
FitInfinityAPI api(baseUrl, deviceId, accessKey);

// I2C timing management variables
unsigned long lastI2CTime = 0;
const unsigned long I2C_INTERVAL = 200;  // minimum 200ms interval between I2C access

void soundSuccess() {
  // Use DFPlayer instead of buzzer for success sound
  Serial.println("Playing success sound...");
  dfplayer.volume(30);  // Volume (0–30)
  dfplayer.playMp3Folder(1);  // Play 001.mp3 from /mp3 folder
}

void soundError() {
  // Use buzzer for error sound
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
  
  // Initialize DFPlayer Mini
  dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX); // RX=16, TX=17
  Serial.println("Initializing DFPlayer...");

  if (!dfplayer.begin(dfSerial)) {
    Serial.println("Failed to communicate with DFPlayer Mini.");
    while (true);
  }

  Serial.println("DFPlayer ready.");
  dfplayer.volume(25);  // Volume (0–30)
  dfplayer.playMp3Folder(1);  // Play 001.mp3 from /mp3 folder

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
  
  if (api.begin(ssid, password)) {
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

  // Manage I2C usage: only allow access every 200ms
  unsigned long now = millis();
  
  if (api.getPendingEnrollments(enrollments)) {
    for (JsonObject enrollment : enrollments) {
      const char* id = enrollment["id"];
      const char* nama = enrollment["nama"];
      int fingerId = enrollment["finger_id"].as<int>();

      if (now - lastI2CTime > I2C_INTERVAL) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("New Enrollment:");
        lcd.setCursor(0, 1);
        lcd.print(nama);
        lastI2CTime = now;
      }

      delay(2000);

      if (now - lastI2CTime > I2C_INTERVAL) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Place finger");
        lcd.setCursor(0, 1);
        lcd.print("on sensor...");
        lastI2CTime = now;
      }

      // Process enrollment
      bool success = api.enrollFingerprint(fingerId);
      if (success) {
        Serial.printf("Successfully enrolled fingerprint for %s\n", nama);
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Enrollment OK!");
          lastI2CTime = millis();
        }
        soundSuccess();
        api.updateEnrollmentStatus(id, fingerId, true);
      } else {
        Serial.printf("Failed to enroll: %s\n", api.getLastError());
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Enrollment");
          lcd.setCursor(0, 1);
          lcd.print("Failed!");
          lastI2CTime = millis();
        }
        soundError();
        api.updateEnrollmentStatus(id, fingerId, false);
      }
      delay(2000);
    }
  }

  // Fingerprint check
  if (api.isConnected()) {
    if (now - lastI2CTime > I2C_INTERVAL) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Ready to Scan");
      lastI2CTime = now;
    }

    int fingerprintId = -1;
    uint8_t result = api.scanFingerprint(&fingerprintId);

    if (result == FINGERPRINT_OK && fingerprintId >= 0) {
      if (api.logFingerprint(fingerprintId)) {
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Fingerprint");
          lcd.setCursor(0, 1);
          lcd.print("Success! ID:");
          lcd.print(fingerprintId);
          lastI2CTime = millis();
        }
        soundSuccess();
      } else {
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Fingerprint");
          lcd.setCursor(0, 1);
          lcd.print("Failed!");
          lastI2CTime = millis();
        }
        soundError();
      }
      delay(2000);
    }

    // RFID Scan
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

      if (millis() - lastI2CTime > I2C_INTERVAL) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Card Found:");
        lcd.setCursor(0, 1);
        lcd.print(rfidTag);
        lastI2CTime = millis();
      }

      if (api.logRFID(rfidTag.c_str())) {
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("RFID Success!");
          lastI2CTime = millis();
        }
        soundSuccess();
      } else {
        if (millis() - lastI2CTime > I2C_INTERVAL) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("RFID Failed!");
          lastI2CTime = millis();
        }
        soundError();
      }

      delay(2000);
    }

    // Uncomment the line below if you want to enable offline record syncing
    // api.syncOfflineRecords();
  }

  delay(100);
}