#include <FitInfinityMQTT.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>

// Device configuration
const char* deviceId = "ESP32_001";
const char* baseUrl = "https://your-fitinfinity-domain.com";
const char* accessKey = "your-access-key";

// MQTT configuration
const char* mqttServer = "your-mqtt-server";
const int mqttPort = 1883;
const char* mqttUsername = "fitinfinity_mqtt";
const char* mqttPassword = "mqtt_p@ssw0rd_f1n1t3";

// Pin definitions
const int FINGER_RX = 19;
const int FINGER_TX = 18;
const int DF_RX = 16;      // DFPlayer RX pin
const int DF_TX = 17;      // DFPlayer TX pin
const int BUZZER_PIN = 4;  // Buzzer pin (used for error sounds)
const int STATUS_LED = 2;
const int CONFIG_BUTTON = 0;

// LCD configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// DFPlayer Mini setup
HardwareSerial dfSerial(1); // UART1
DFRobotDFPlayerMini dfplayer;

// Initialize components
HardwareSerial fingerSerial(2);
PN532_I2C pn532_i2c(Wire);
PN532 nfc(pn532_i2c);
FitInfinityMQTT api(baseUrl, deviceId, accessKey);

// State management
bool enrollmentMode = false;
String currentEnrollmentId = "";
int currentFingerprintSlot = -1;
String currentEmployeeName = "";
unsigned long lastStatusUpdate = 0;
bool systemReady = false;

// Function prototypes
void onEnrollmentRequest(String employeeId, String employeeName, int fingerprintSlot);
void onFirmwareUpdate(String version, String downloadUrl, String checksum);
void onModeChange(bool enrollmentMode);
void onWifiConfig(String ssid, String password);
void handleFingerprint();
void handleRFID();
void showStatus(String message, String detail = "");
void soundSuccess();
void soundError();
void playBeep(int duration = 100);
void blinkLED(int times = 1);
void checkConfigButton();

void setup() {
    Serial.begin(115200);
    Serial.println("\nFitInfinity MQTT System v1.0");
    Serial.println("=============================");
    
    // Initialize pins
    pinMode(STATUS_LED, OUTPUT);
    pinMode(CONFIG_BUTTON, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Initialize DFPlayer Mini
    dfSerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX); // RX=16, TX=17
    Serial.println("Initializing DFPlayer...");

    if (!dfplayer.begin(dfSerial)) {
        Serial.println("Failed to communicate with DFPlayer Mini.");
        showStatus("DFPlayer Error", "Check wiring");
        delay(2000);
    } else {
        Serial.println("DFPlayer ready.");
        dfplayer.volume(25);  // Volume (0–30)
        dfplayer.playMp3Folder(1);  // Play 001.mp3 from /mp3 folder for startup
    }
    
    // Initialize LCD
    lcd.init();
    lcd.backlight();
    showStatus("FitInfinity", "Initializing...");
    
    // Initialize I2C components
    Wire.begin();
    
    // Initialize NFC
    nfc.begin();
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        Serial.println("Warning: NFC module not found");
        showStatus("Warning", "NFC not found");
        delay(2000);
    } else {
        Serial.print("Found NFC chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
        nfc.setPassiveActivationRetries(0xFF);
        nfc.SAMConfig();
    }
    
    // Initialize fingerprint sensor
    fingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
    if (api.beginFingerprint(&fingerSerial)) {
        Serial.println("Fingerprint sensor initialized");
    } else {
        Serial.println("Warning: Fingerprint sensor not found");
        showStatus("Warning", "Fingerprint err");
        delay(2000);
    }
    
    // Try to load saved WiFi credentials
    String savedSSID, savedPassword;
    bool hasCredentials = api.loadWifiCredentials(savedSSID, savedPassword);
    
    if (hasCredentials) {
        Serial.println("Found saved WiFi credentials: " + savedSSID);
        showStatus("Connecting WiFi", savedSSID);
        
        if (api.connectWifi(savedSSID, savedPassword)) {
            Serial.println("Connected to WiFi!");
            showStatus("WiFi Connected", WiFi.localIP().toString());
            blinkLED(3);
            delay(1000);
        } else {
            Serial.println("Failed to connect with saved credentials");
            startWifiConfig();
        }
    } else {
        Serial.println("No saved WiFi credentials found");
        startWifiConfig();
    }
    
    // Set firmware version
    api.setFirmwareVersion("1.0.0");
    
    // Connect to MQTT
    showStatus("Connecting MQTT", "Please wait...");
    if (api.connectMQTT(mqttServer, mqttPort, mqttUsername, mqttPassword)) {
        Serial.println("Connected to MQTT!");
        showStatus("MQTT Connected", "System Ready");
        
        // Register callbacks
        api.onEnrollmentRequest(onEnrollmentRequest);
        api.onFirmwareUpdate(onFirmwareUpdate);
        api.onModeChange(onModeChange);
        api.onWifiConfig(onWifiConfig);
        
        // Subscribe to WiFi configuration updates
        api.subscribeWifiConfig();
        
        // Send initial device status
        api.publishDeviceStatus("online");
        api.publishDeviceMetrics();
        
        systemReady = true;
        blinkLED(5);
        playBeep(200);
        
    } else {
        Serial.println("Failed to connect to MQTT");
        showStatus("MQTT Failed", "Check connection");
        blinkLED(10);
    }
    
    Serial.println("Setup complete. System ready.");
}

void loop() {
    // Handle MQTT communication
    api.mqttLoop();
    
    // Check configuration button
    checkConfigButton();
    
    // Send periodic status updates
    if (millis() - lastStatusUpdate > 30000) { // Every 30 seconds
        if (api.isMQTTConnected()) {
            api.publishHeartbeat();
            api.publishDeviceMetrics();
        }
        lastStatusUpdate = millis();
    }
    
    // Only process attendance if system is ready and not in enrollment mode
    if (systemReady && !enrollmentMode) {
        showStatus("Ready to Scan", "Finger or RFID");
        handleFingerprint();
        handleRFID();
    }
    
    // Handle enrollment mode
    if (enrollmentMode && currentFingerprintSlot >= 0) {
        showStatus("Enrollment Mode", currentEmployeeName);
        lcd.setCursor(0, 1);
        lcd.print("Place finger...");
        
        bool success = api.enrollFingerprint(currentFingerprintSlot);
        if (success) {
            Serial.println("Enrollment successful for " + currentEmployeeName);
            showStatus("Enrollment OK!", currentEmployeeName);
            api.publishEnrollmentStatus(currentEnrollmentId, "enrolled", currentFingerprintSlot);
            soundSuccess(); // Play 001.mp3 for success
            blinkLED(3);
        } else {
            Serial.println("Enrollment failed for " + currentEmployeeName);
            showStatus("Enrollment Failed", "Try again");
            api.publishEnrollmentStatus(currentEnrollmentId, "failed");
            soundError(); // Play buzzer for error
        }
        
        // Reset enrollment state
        enrollmentMode = false;
        currentEnrollmentId = "";
        currentFingerprintSlot = -1;
        currentEmployeeName = "";
        delay(2000);
    }
    
    delay(100);
}

// Callback functions
void onEnrollmentRequest(String employeeId, String employeeName, int fingerprintSlot) {
    Serial.println("Enrollment request received:");
    Serial.println("  Employee: " + employeeName);
    Serial.println("  ID: " + employeeId);
    Serial.println("  Slot: " + String(fingerprintSlot));
    
    currentEnrollmentId = employeeId;
    currentEmployeeName = employeeName;
    currentFingerprintSlot = fingerprintSlot;
    enrollmentMode = true;
    
    showStatus("Enrollment Mode", employeeName);
    api.publishEnrollmentStatus(employeeId, "in_progress");
    playBeep(300);
}

void onFirmwareUpdate(String version, String downloadUrl, String checksum) {
    Serial.println("Firmware update available:");
    Serial.println("  Version: " + version);
    Serial.println("  URL: " + downloadUrl);
    Serial.println("  Checksum: " + checksum);
    
    showStatus("Firmware Update", "Version " + version);
    playBeep(200);
    delay(500);
    playBeep(200);
    
    // Start firmware download and installation
    bool success = api.downloadAndInstallFirmware(downloadUrl, version, checksum);
    if (success) {
        showStatus("Update Success", "Restarting...");
        delay(2000);
        ESP.restart();
    } else {
        showStatus("Update Failed", "Continue normal");
        delay(3000);
    }
}

void onModeChange(bool enrollmentModeEnabled) {
    Serial.println("Enrollment mode " + String(enrollmentModeEnabled ? "enabled" : "disabled"));
    enrollmentMode = enrollmentModeEnabled;
    
    if (enrollmentModeEnabled) {
        showStatus("Enrollment Mode", "Enabled");
    } else {
        showStatus("Normal Mode", "Ready to scan");
    }
    
    playBeep(150);
}

void onWifiConfig(String ssid, String password) {
    Serial.println("WiFi configuration received:");
    Serial.println("  SSID: " + ssid);
    
    showStatus("WiFi Config", "Connecting...");
    
    // Save and connect to new WiFi
    api.saveWifiCredentials(ssid, password);
    
    if (api.connectWifi(ssid, password)) {
        showStatus("WiFi Updated", "Connected");
        Serial.println("WiFi configuration successful");
        blinkLED(3);
        
        // Restart to use new connection
        delay(2000);
        ESP.restart();
    } else {
        showStatus("WiFi Failed", "Check settings");
        Serial.println("WiFi configuration failed");
        blinkLED(10);
    }
}

void handleFingerprint() {
    int fingerprintId = -1;
    uint8_t result = api.scanFingerprint(&fingerprintId);
    
    if (result == FINGERPRINT_OK && fingerprintId >= 0) {
        String timestamp = api.getTimestamp();
        api.publishAttendanceLog("fingerprint", String(fingerprintId), timestamp);
        
        showStatus("Fingerprint OK", "ID: " + String(fingerprintId));
        soundSuccess(); // Play 001.mp3 for success
        blinkLED(2);
        
        Serial.println("Fingerprint attendance: ID " + String(fingerprintId));
        delay(1500);
    }
}

void handleRFID() {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        String rfidTag = "";
        for (uint8_t i = 0; i < uidLength; i++) {
            if (uid[i] < 0x10) rfidTag += "0";
            rfidTag += String(uid[i], HEX);
        }
        rfidTag.toUpperCase();
        
        String timestamp = api.getTimestamp();
        api.publishAttendanceLog("rfid", rfidTag, timestamp);
        
        showStatus("RFID Success", rfidTag);
        soundSuccess(); // Play 001.mp3 for success
        blinkLED(2);
        
        Serial.println("RFID attendance: " + rfidTag);
        delay(1500);
    }
}

void startWifiConfig() {
    Serial.println("Starting WiFi configuration mode...");
    showStatus("WiFi Setup Mode", "Connect to AP");
    
    // Start access point
    api.startAccessPoint("FitInfinity-Config", "fitinfinity123");
    api.startConfigServer();
    
    // Display instructions
    Serial.println("================================");
    Serial.println("WiFi Configuration Mode Active");
    Serial.println("1. Connect to WiFi: FitInfinity-Config");
    Serial.println("2. Password: fitinfinity123");
    Serial.println("3. Open browser to: http://192.168.4.1");
    Serial.println("4. Select your WiFi network");
    Serial.println("5. Enter password and save");
    Serial.println("================================");
    
    // Show rotating instructions on LCD
    unsigned long lastInstruction = 0;
    int instructionStep = 0;
    
    while (!WiFi.isConnected()) {
        // Handle config server requests
        api.mqttLoop(); // This handles the config server as well
        
        // Rotate instructions every 3 seconds
        if (millis() - lastInstruction > 3000) {
            switch (instructionStep) {
                case 0:
                    showStatus("1.Connect WiFi:", "FitInfinity-Config");
                    break;
                case 1:
                    showStatus("2.Password:", "fitinfinity123");
                    break;
                case 2:
                    showStatus("3.Open Browser:", "192.168.4.1");
                    break;
                case 3:
                    showStatus("4.Select Network", "& Enter Password");
                    break;
            }
            instructionStep = (instructionStep + 1) % 4;
            lastInstruction = millis();
        }
        
        delay(100);
    }
    
    api.stopConfigServer();
    showStatus("WiFi Connected!", WiFi.localIP().toString());
    Serial.println("WiFi configured successfully!");
    delay(2000);
}

void checkConfigButton() {
    static unsigned long buttonPressTime = 0;
    static bool buttonPressed = false;
    
    if (digitalRead(CONFIG_BUTTON) == LOW) {
        if (!buttonPressed) {
            buttonPressed = true;
            buttonPressTime = millis();
        } else if (millis() - buttonPressTime > 5000) { // 5 seconds
            Serial.println("Config button held - starting WiFi configuration");
            showStatus("Config Mode", "Release button");
            
            // Wait for button release
            while (digitalRead(CONFIG_BUTTON) == LOW) {
                delay(100);
            }
            
            startWifiConfig();
            buttonPressed = false;
        }
    } else {
        buttonPressed = false;
    }
}

void showStatus(String message, String detail) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(message);
    if (detail.length() > 0) {
        lcd.setCursor(0, 1);
        lcd.print(detail);
    }
}

void soundSuccess() {
    // Use DFPlayer for success sound
    Serial.println("Playing success sound...");
    dfplayer.volume(30);  // Volume (0–30)
    dfplayer.playMp3Folder(1);  // Play 001.mp3 from /mp3 folder
}

void soundError() {
    // Use DFPlayer for error sound
    Serial.println("Playing error sound...");
    dfplayer.volume(30);  // Volume (0–30)
    dfplayer.playMp3Folder(2);  // Play 002.mp3 from /mp3 folder
}

void playBeep(int duration) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
}

void blinkLED(int times) {
    for (int i = 0; i < times; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(100);
        digitalWrite(STATUS_LED, LOW);
        delay(100);
    }
}