#include "FitInfinityMQTT.h"

FitInfinityMQTT::FitInfinityMQTT(const char* baseUrl, const char* deviceId, const char* accessKey) 
    : FitInfinityAPI(baseUrl, accessKey), mqttClient(wifiClient), deviceId(deviceId) {
    
    // Initialize variables
    lastHeartbeat = 0;
    lastReconnectAttempt = 0;
    reconnectAttempts = 0;
    enrollmentMode = false;
    wifiConfigMode = false;
    
    // Initialize callback pointers
    enrollmentCallback = nullptr;
    firmwareUpdateCallback = nullptr;
    modeChangeCallback = nullptr;
    wifiConfigCallback = nullptr;
    
    // Set firmware version
    currentFirmwareVersion = "1.0.0";
    
    // Initialize config server and DNS server pointers
    configServer = nullptr;
    dnsServer = nullptr;
}

bool FitInfinityMQTT::connectMQTT(const char* server, int port, const char* username, const char* password) {
    mqttServer = String(server);
    mqttPort = port;
    mqttUsername = String(username);
    mqttPassword = String(password);
    
    Serial.println("Connecting to MQTT broker...");
    Serial.println("Server: " + mqttServer + ":" + String(mqttPort));
    
    mqttClient.setServer(server, port);
    mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->handleMqttMessage(topic, payload, length);
    });
    
    // Set client options
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(10);
    
    return reconnectMQTT();
}

bool FitInfinityMQTT::reconnectMQTT() {
    if (mqttClient.connected()) {
        return true;
    }
    
    if (millis() - lastReconnectAttempt < 5000) {
        return false; // Don't retry too frequently
    }
    
    lastReconnectAttempt = millis();
    reconnectAttempts++;
    
    String clientId = "FitInfinity-" + deviceId + "-" + String(random(0xffff), HEX);
    
    Serial.print("Attempting MQTT connection... ");
    Serial.println("Client ID: " + clientId);
    
    if (mqttClient.connect(clientId.c_str(), mqttUsername.c_str(), mqttPassword.c_str())) {
        Serial.println("MQTT connected!");
        reconnectAttempts = 0;
        
        // Setup subscriptions
        setupSubscriptions();
        
        // Publish online status
        publishDeviceStatus("online");
        publishDeviceMetrics();
        
        return true;
    } else {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" retrying in 5 seconds");
        return false;
    }
}

void FitInfinityMQTT::setupSubscriptions() {
    String topicPrefix = getTopicPrefix();
    
    // Subscribe to enrollment topics
    mqttClient.subscribe((topicPrefix + "/enrollment/request").c_str());
    mqttClient.subscribe((topicPrefix + "/enrollment/mode/switch").c_str());
    
    // Subscribe to OTA topics
    mqttClient.subscribe((topicPrefix + "/ota/available").c_str());
    mqttClient.subscribe((topicPrefix + "/ota/download").c_str());
    
    // Subscribe to configuration topics
    mqttClient.subscribe((topicPrefix + "/config/wifi/response").c_str());
    mqttClient.subscribe((topicPrefix + "/config/wifi/scan").c_str());
    
    // Subscribe to command topics
    mqttClient.subscribe((topicPrefix + "/commands/+").c_str());
    
    // Subscribe to system broadcasts
    mqttClient.subscribe("fitinfinity/system/broadcast/+");
    
    Serial.println("MQTT subscriptions setup complete");
}

void FitInfinityMQTT::handleMqttMessage(char* topic, byte* payload, unsigned int length) {
    // Convert payload to string
    String message = "";
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    String topicStr = String(topic);
    Serial.println("MQTT Message received:");
    Serial.println("Topic: " + topicStr);
    Serial.println("Payload: " + message);
    
    // Parse JSON payload
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.println("Failed to parse JSON message");
        return;
    }
    
    // Handle enrollment requests
    if (topicStr.endsWith("/enrollment/request")) {
        String employeeId = doc["employeeId"];
        String employeeName = doc["employeeName"];
        int fingerprintSlot = doc["fingerprintSlot"];
        
        if (enrollmentCallback) {
            enrollmentCallback(employeeId, employeeName, fingerprintSlot);
        }
    }
    // Handle enrollment mode switch
    else if (topicStr.endsWith("/enrollment/mode/switch")) {
        bool enabled = doc["enrollmentMode"];
        enrollmentMode = enabled;
        
        if (modeChangeCallback) {
            modeChangeCallback(enabled);
        }
    }
    // Handle OTA firmware updates
    else if (topicStr.endsWith("/ota/available")) {
        String version = doc["version"];
        String downloadUrl = doc["downloadUrl"];
        String checksum = doc["checksum"];
        
        if (firmwareUpdateCallback) {
            firmwareUpdateCallback(version, downloadUrl, checksum);
        }
    }
    // Handle WiFi configuration
    else if (topicStr.endsWith("/config/wifi/response")) {
        String ssid = doc["ssid"];
        String password = doc["password"];
        
        if (wifiConfigCallback) {
            wifiConfigCallback(ssid, password);
        } else {
            // Default WiFi configuration handling
            handleWifiConfig(ssid, password);
        }
    }
    // Handle WiFi scan requests
    else if (topicStr.endsWith("/config/wifi/scan")) {
        scanWifiNetworks();
    }
    // Handle system broadcasts
    else if (topicStr.startsWith("fitinfinity/system/broadcast/")) {
        String broadcastType = topicStr.substring(topicStr.lastIndexOf("/") + 1);
        String broadcastMessage = doc["message"];
        
        Serial.println("System broadcast (" + broadcastType + "): " + broadcastMessage);
        
        // Handle maintenance mode
        if (broadcastType == "maintenance") {
            bool maintenanceMode = doc["data"]["enabled"];
            if (maintenanceMode) {
                Serial.println("Entering maintenance mode");
                // Could display maintenance message on LCD
            }
        }
    }
}

void FitInfinityMQTT::mqttLoop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    } else {
        mqttClient.loop();
        
        // Send periodic heartbeat
        if (millis() - lastHeartbeat > 30000) { // Every 30 seconds
            sendHeartbeat();
            lastHeartbeat = millis();
        }
    }
    
    // Handle WiFi config server if active
    if (wifiConfigMode && configServer) {
        configServer->handleClient();
        if (dnsServer) {
            dnsServer->processNextRequest();
        }
    }
}

bool FitInfinityMQTT::isMQTTConnected() {
    return mqttClient.connected();
}

void FitInfinityMQTT::disconnectMQTT() {
    if (mqttClient.connected()) {
        publishDeviceStatus("offline");
        mqttClient.disconnect();
    }
}

// Callback registration
void FitInfinityMQTT::onEnrollmentRequest(void (*callback)(String, String, int)) {
    enrollmentCallback = callback;
}

void FitInfinityMQTT::onFirmwareUpdate(void (*callback)(String, String, String)) {
    firmwareUpdateCallback = callback;
}

void FitInfinityMQTT::onModeChange(void (*callback)(bool)) {
    modeChangeCallback = callback;
}

void FitInfinityMQTT::onWifiConfig(void (*callback)(String, String)) {
    wifiConfigCallback = callback;
}

// Enrollment functions
void FitInfinityMQTT::publishEnrollmentStatus(String employeeId, String status, int fingerprintId) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["employeeId"] = employeeId;
    doc["status"] = status;
    doc["timestamp"] = getTimestamp();
    
    if (fingerprintId >= 0) {
        doc["fingerprintId"] = fingerprintId;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/enrollment/status";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published enrollment status: " + status);
}

void FitInfinityMQTT::setEnrollmentMode(bool enabled) {
    enrollmentMode = enabled;
    
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(256);
    doc["deviceId"] = deviceId;
    doc["enrollmentMode"] = enabled;
    doc["timestamp"] = getTimestamp();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/enrollment/mode";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Set enrollment mode: " + String(enabled ? "enabled" : "disabled"));
}

// Attendance functions
void FitInfinityMQTT::publishAttendanceLog(String type, String id, String timestamp) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["type"] = type;
    doc["id"] = id;
    doc["timestamp"] = timestamp;
    doc["location"] = deviceId; // Device location identifier
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/attendance/" + type;
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published " + type + " attendance: " + id);
}

void FitInfinityMQTT::publishBulkAttendanceData(JsonArray attendanceData) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(2048);
    doc["deviceId"] = deviceId;
    doc["attendanceData"] = attendanceData;
    doc["timestamp"] = getTimestamp();
    doc["count"] = attendanceData.size();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/attendance/bulk";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published bulk attendance data: " + String(attendanceData.size()) + " records");
}

// Device management functions
void FitInfinityMQTT::publishHeartbeat() {
    sendHeartbeat();
}

void FitInfinityMQTT::sendHeartbeat() {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["timestamp"] = getTimestamp();
    doc["uptime"] = getUptime();
    doc["freeHeap"] = getFreeHeap();
    doc["wifiRSSI"] = getSignalStrength();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/status/heartbeat";
    mqttClient.publish(topic.c_str(), payload.c_str());
}

void FitInfinityMQTT::publishDeviceStatus(String status) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["status"] = status;
    doc["timestamp"] = getTimestamp();
    doc["firmwareVersion"] = currentFirmwareVersion;
    doc["ipAddress"] = WiFi.localIP().toString();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/status/online";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published device status: " + status);
}

void FitInfinityMQTT::publishDeviceError(String error) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["error"] = error;
    doc["timestamp"] = getTimestamp();
    doc["firmwareVersion"] = currentFirmwareVersion;
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/status/error";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published device error: " + error);
}

void FitInfinityMQTT::publishDeviceMetrics() {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(1024);
    doc["deviceId"] = deviceId;
    doc["timestamp"] = getTimestamp();
    doc["metrics"]["uptime"] = getUptime();
    doc["metrics"]["freeHeap"] = getFreeHeap();
    doc["metrics"]["wifiRSSI"] = getSignalStrength();
    doc["metrics"]["temperature"] = getTemperature();
    doc["metrics"]["firmwareVersion"] = currentFirmwareVersion;
    doc["metrics"]["wifiSSID"] = WiFi.SSID();
    doc["metrics"]["ipAddress"] = WiFi.localIP().toString();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/status/metrics";
    mqttClient.publish(topic.c_str(), payload.c_str());
}

// Helper functions
String FitInfinityMQTT::getTopicPrefix() {
    return "fitinfinity/devices/" + deviceId;
}

String FitInfinityMQTT::getDeviceInfo() {
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["firmwareVersion"] = currentFirmwareVersion;
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreq"] = ESP.getCpuFreqMHz();
    doc["flashSize"] = ESP.getFlashChipSize();
    doc["freeHeap"] = ESP.getFreeHeap();
    
    String result;
    serializeJson(doc, result);
    return result;
}

String FitInfinityMQTT::getFirmwareVersion() {
    return currentFirmwareVersion;
}

void FitInfinityMQTT::setFirmwareVersion(String version) {
    currentFirmwareVersion = version;
}

float FitInfinityMQTT::getTemperature() {
    // ESP32 internal temperature sensor (approximate)
    return temperatureRead();
}

int FitInfinityMQTT::getSignalStrength() {
    return WiFi.RSSI();
}

long FitInfinityMQTT::getUptime() {
    return millis() / 1000; // Return uptime in seconds
}

int FitInfinityMQTT::getFreeHeap() {
    return ESP.getFreeHeap();
}

// Continue with WiFi and OTA functions in the next part...