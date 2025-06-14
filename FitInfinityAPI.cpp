#include "FitInfinityAPI.h"
#include <time.h>

const char* FitInfinityAPI::OFFLINE_FILE = "/offline.txt";
const char* FitInfinityAPI::OFFLINE_TEMP = "/offline.tmp";

FitInfinityAPI::FitInfinityAPI(const char* baseUrl, const char* deviceId, const char* accessKey) {
    _fingerSensor = nullptr;
    _baseUrl = String(baseUrl);
    _deviceId = String(deviceId);
    _accessKey = String(accessKey);
    _ntpServer = "pool.ntp.org";
    _timeout = 10000;
    _isConnected = false;
    _lastResponseCode = 0;
    _useSDCard = false;
    _sdCardPin = -1;
}

bool FitInfinityAPI::begin(const char* ssid, const char* password, int8_t sdCardPin) {
    _sdCardPin = sdCardPin;
    WiFi.begin(ssid, password);
    
    // Wait for connection with timeout
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < _timeout) {
        delay(500);
    }
    
    _isConnected = (WiFi.status() == WL_CONNECTED);
    
    if (_isConnected) {
        initTimeSync();
        
        // Initialize SD card if pin provided
        if (_sdCardPin >= 0) {
            _useSDCard = initSDCard();
            if (!_useSDCard) {
                Serial.println("SD card initialization failed, falling back to memory storage");
            }
        }
        
        return authenticate();
    }
    
    _lastError = "WiFi connection failed";
    return false;
}

bool FitInfinityAPI::authenticate() {
    StaticJsonDocument<200> doc;
    doc["deviceId"] = _deviceId;
    doc["accessKey"] = _accessKey;
    
    return makeRequest("authenticate", doc);
}

bool FitInfinityAPI::logFingerprint(int fingerId) {
    if (!isConnected()) {
        storeOfflineRecord("fingerprint", String(fingerId).c_str(), getTimestamp().c_str());
        return false;
    }
    
    StaticJsonDocument<200> doc;
    doc["deviceId"] = _deviceId;
    doc["accessKey"] = _accessKey;
    doc["fingerId"] = fingerId;
    doc["timestamp"] = getTimestamp();
    
    return makeRequest("logFingerprint", doc);
}

bool FitInfinityAPI::logRFID(const char* rfidNumber) {
    if (!isConnected()) {
        storeOfflineRecord("rfid", rfidNumber, getTimestamp().c_str());
        return false;
    }
    
    StaticJsonDocument<200> doc;
    doc["deviceId"] = _deviceId;
    doc["accessKey"] = _accessKey;
    doc["rfid"] = rfidNumber;
    doc["timestamp"] = getTimestamp();
    
    return makeRequest("logRFID", doc);
}

bool FitInfinityAPI::getPendingEnrollments(JsonArray& result) {
    if (!isConnected()) {
        _lastError = "Not connected to network";
        return false;
    }

    HTTPClient http;
    String url = _baseUrl + "/api/esp32/enrollments/pending";
    // Add authentication parameters
    url += "?deviceId=" + _deviceId + "&accessKey=" + _accessKey;
    http.begin(url);
    
    int httpCode = http.GET();
    bool success = (httpCode == HTTP_CODE_OK);
    
    if (success) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            if (doc.containsKey("status") && doc["status"] == "none") {
                // No pending enrollments
                success = true;
            } else {
                // Single enrollment response
                JsonObject item = result.createNestedObject();
                item["id"] = doc["id"];
                item["nama"] = doc["nama"];
                // Default finger_id to 0, will be assigned during enrollment
                item["finger_id"] = 0;
                item["status"] = "PENDING";
                // Add current timestamp as created_at
                item["created_at"] = getTimestamp();
            }
        } else {
            _lastError = "JSON parsing failed";
            success = false;
        }
    } else {
        _lastError = http.getString();
    }
    
    _lastResponseCode = httpCode;
    http.end();
    return success;
}

bool FitInfinityAPI::beginFingerprint(Stream* stream) {
    if (!stream) {
        _lastError = "Invalid stream for fingerprint sensor";
        return false;
    }

    _fingerSensor = new Adafruit_Fingerprint(stream);
    _fingerSensor->begin(57600);

    if (!_fingerSensor->verifyPassword()) {
        _lastError = "Fingerprint sensor not found";
        delete _fingerSensor;
        _fingerSensor = nullptr;
        return false;
    }

    return true;
}

bool FitInfinityAPI::enrollFingerprint(int id) {
    if (!_fingerSensor) {
        _lastError = "Fingerprint sensor not initialized";
        return false;
    }

    // Take first fingerprint image
    while (_fingerSensor->getImage() != FINGERPRINT_OK) {
        delay(100);
    }

    if (_fingerSensor->image2Tz(1) != FINGERPRINT_OK) {
        _lastError = "Failed to process first image";
        return false;
    }

    delay(2000);

    // Wait until finger is removed
    while (_fingerSensor->getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
    }

    delay(1000);

    // Take second fingerprint image
    while (_fingerSensor->getImage() != FINGERPRINT_OK) {
        delay(100);
    }

    if (_fingerSensor->image2Tz(2) != FINGERPRINT_OK) {
        _lastError = "Failed to process second image";
        return false;
    }

    if (_fingerSensor->createModel() != FINGERPRINT_OK) {
        _lastError = "Failed to create fingerprint model";
        return false;
    }

    if (_fingerSensor->storeModel(id) != FINGERPRINT_OK) {
        _lastError = "Failed to store fingerprint model";
        return false;
    }

    return true;
}

bool FitInfinityAPI::updateEnrollmentStatus(const char* employeeId, int fingerprintId, bool success) {
    if (!isConnected()) {
        _lastError = "Not connected to network";
        return false;
    }

    HTTPClient http;
    String url = _baseUrl + "/api/esp32/enrollments/status";
    url += "?deviceId=" + _deviceId + "&accessKey=" + _accessKey;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["employeeId"] = employeeId;
    doc["fingerprintId"] = fingerprintId;
    doc["status"] = success ? "ENROLLED" : "FAILED";

    String jsonStr;
    serializeJson(doc, jsonStr);

    int httpCode = http.POST(jsonStr);
    bool requestSuccess = (httpCode == HTTP_CODE_OK);

    if (!requestSuccess) {
        _lastError = http.getString();
    }

    http.end();
    return requestSuccess;
}

uint8_t FitInfinityAPI::scanFingerprint(int* fingerprintId) {
    if (!_fingerSensor) {
        _lastError = "Fingerprint sensor not initialized";
        return FINGERPRINT_NONE;
    }

    uint8_t result = _fingerSensor->getImage();
    if (result != FINGERPRINT_OK) {
        return result;
    }

    result = _fingerSensor->image2Tz();
    if (result != FINGERPRINT_OK) {
        _lastError = "Failed to convert image";
        return result;
    }

    result = _fingerSensor->fingerSearch();
    if (result != FINGERPRINT_OK) {
        _lastError = "No matching fingerprint found";
        return result;
    }

    // Store the matched fingerprint ID
    if (fingerprintId) {
        *fingerprintId = _fingerSensor->fingerID;
    }

    return FINGERPRINT_OK;
}

void FitInfinityAPI::setOfflineStorageMode(bool useSD) {
    if (useSD && _sdCardPin >= 0) {
        _useSDCard = initSDCard();
    } else {
        _useSDCard = false;
    }
}

bool FitInfinityAPI::isSDCardEnabled() {
    return _useSDCard;
}

String FitInfinityAPI::getOfflineStorageStats() {
    if (!_useSDCard) {
        return "SD card not enabled";
    }
    
    File file = SD.open(OFFLINE_FILE);
    if (!file) {
        return "No offline records";
    }
    
    size_t size = file.size();
    file.close();
    
    return "Offline file size: " + String(size) + " bytes";
}

void FitInfinityAPI::storeOfflineRecord(const char* type, const char* id, const char* timestamp) {
    if (_useSDCard) {
        writeToSDCard(type, id, timestamp);
        return;
    }
    
    // Fallback to memory storage
    StaticJsonDocument<200> doc;
    doc["type"] = type;
    doc["id"] = id;
    doc["timestamp"] = timestamp;
    
    File file = SD.open(OFFLINE_FILE, FILE_APPEND);
    if (file) {
        serializeJson(doc, file);
        file.println();
        file.close();
    }
}

bool FitInfinityAPI::syncOfflineRecords() {
    if (!isConnected()) {
        return false;
    }
    
    StaticJsonDocument<1024> doc;
    doc["deviceId"] = _deviceId;
    doc["accessKey"] = _accessKey;
    
    if (_useSDCard) {
        File file = SD.open(OFFLINE_FILE);
        if (!file) {
            return false;
        }
        
        JsonArray records = doc.createNestedArray("records");
        int recordCount = 0;
        
        while (file.available() && recordCount < 50) {  // Process max 50 records at a time
            String line = file.readStringUntil('\n');
            StaticJsonDocument<200> recordDoc;
            DeserializationError error = deserializeJson(recordDoc, line);
            
            if (!error) {
                JsonObject record = records.createNestedObject();
                record["type"] = recordDoc["type"];
                record["id"] = recordDoc["id"];
                record["timestamp"] = recordDoc["timestamp"];
                recordCount++;
            }
        }
        
        file.close();
        
        if (recordCount > 0) {
            bool success = makeRequest("bulkLog", doc);
            if (success) {
                clearProcessedRecords(recordCount);
            }
            return success;
        }
        return true;  // No records to process
    } else {
        // Fallback to memory-based storage (implementation remains same)
        return false;
    }
    return success;
}

bool FitInfinityAPI::isConnected() {
    updateConnectionStatus();
    return _isConnected;
}

String FitInfinityAPI::getLastError() {
    return _lastError;
}

int FitInfinityAPI::getLastResponseCode() {
    return _lastResponseCode;
}

String FitInfinityAPI::getTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char timestamp[25];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", &timeinfo);
        return String(timestamp);
    }
    return "";
}

void FitInfinityAPI::setNTPServer(const char* server) {
    _ntpServer = String(server);
    initTimeSync();
}

void FitInfinityAPI::setTimeout(uint16_t timeoutMs) {
    _timeout = timeoutMs;
}

// Private methods
bool FitInfinityAPI::makeRequest(const char* action, JsonDocument& doc) {
    if (!isConnected()) {
        _lastError = "Not connected to network";
        return false;
    }
    
    HTTPClient http;
    String url = _baseUrl;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    // Add action to the request body
    doc["action"] = action;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    
    _lastResponseCode = http.POST(jsonStr);
    bool success = (_lastResponseCode == HTTP_CODE_OK);
    
    if (!success) {
        String response = http.getString();
        StaticJsonDocument<200> errorDoc;
        DeserializationError error = deserializeJson(errorDoc, response);
        if (!error && errorDoc.containsKey("error")) {
            _lastError = errorDoc["error"].as<String>();
        } else {
            _lastError = response;
        }
    }
    
    http.end();
    return success;
}

void FitInfinityAPI::updateConnectionStatus() {
    _isConnected = (WiFi.status() == WL_CONNECTED);
}

void FitInfinityAPI::initTimeSync() {
    configTime(0, 0, _ntpServer.c_str());
}

bool FitInfinityAPI::initSDCard() {
    if (!SD.begin(_sdCardPin)) {
        _lastError = "Failed to initialize SD card";
        return false;
    }
    return true;
}

bool FitInfinityAPI::writeToSDCard(const char* type, const char* id, const char* timestamp) {
    if (!_useSDCard) return false;
    
    File file = SD.open(OFFLINE_FILE, FILE_APPEND);
    if (!file) {
        _lastError = "Could not open offline storage file";
        return false;
    }
    
    StaticJsonDocument<200> doc;
    doc["type"] = type;
    doc["id"] = id;
    doc["timestamp"] = timestamp;
    
    serializeJson(doc, file);
    file.println();
    file.close();
    return true;
}

bool FitInfinityAPI::clearProcessedRecords(int count) {
    if (!_useSDCard || count <= 0) return false;
    
    File sourceFile = SD.open(OFFLINE_FILE);
    File tempFile = SD.open(OFFLINE_TEMP, FILE_WRITE);
    
    if (!sourceFile || !tempFile) {
        return false;
    }
    
    // Skip processed records
    for (int i = 0; i < count; i++) {
        if (sourceFile.available()) {
            sourceFile.readStringUntil('\n');
        }
    }
    
    // Copy remaining records to temp file
    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        tempFile.println(line);
    }
    
    sourceFile.close();
    tempFile.close();
    
    // Replace original file with temp file
    SD.remove(OFFLINE_FILE);
    return renameFile(OFFLINE_TEMP, OFFLINE_FILE);
}

bool FitInfinityAPI::renameFile(const char* from, const char* to) {
    return SD.rename(from, to);
}