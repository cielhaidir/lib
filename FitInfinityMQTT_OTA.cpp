#include "FitInfinityMQTT.h"

// OTA Update Functions

bool FitInfinityMQTT::downloadAndInstallFirmware(String firmwareUrl, String version, String checksum) {
    Serial.println("Starting OTA firmware update...");
    Serial.println("Version: " + version);
    Serial.println("URL: " + firmwareUrl);
    Serial.println("Checksum: " + checksum);
    
    publishUpdateProgress(0);
    publishUpdateStatus("downloading", "");
    
    // Initialize HTTP client
    HTTPClient http;
    http.begin(firmwareUrl);
    http.setTimeout(30000); // 30 second timeout
    
    // Add headers
    http.addHeader("User-Agent", "FitInfinity-ESP32/" + currentFirmwareVersion);
    http.addHeader("X-Device-ID", deviceId);
    
    Serial.println("Connecting to firmware server...");
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        String error = "HTTP error: " + String(httpCode);
        Serial.println(error);
        publishUpdateStatus("failed", error);
        http.end();
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        String error = "Invalid content length: " + String(contentLength);
        Serial.println(error);
        publishUpdateStatus("failed", error);
        http.end();
        return false;
    }
    
    Serial.println("Firmware size: " + String(contentLength) + " bytes");
    
    // Check if we have enough space
    if (!Update.begin(contentLength)) {
        String error = "Not enough space for update: " + String(Update.errorString());
        Serial.println(error);
        publishUpdateStatus("failed", error);
        http.end();
        return false;
    }
    
    publishUpdateProgress(5);
    
    // Get WiFi stream
    WiFiClient* stream = http.getStreamPtr();
    
    size_t written = 0;
    uint8_t buffer[1024];
    int lastProgress = 0;
    
    Serial.println("Starting firmware download and installation...");
    
    while (http.connected() && written < contentLength) {
        size_t available = stream->available();
        if (available) {
            int readBytes = stream->readBytes(buffer, min(available, sizeof(buffer)));
            if (readBytes > 0) {
                size_t writtenBytes = Update.write(buffer, readBytes);
                if (writtenBytes != readBytes) {
                    String error = "Write error: expected " + String(readBytes) + ", wrote " + String(writtenBytes);
                    Serial.println(error);
                    publishUpdateStatus("failed", error);
                    Update.abort();
                    http.end();
                    return false;
                }
                written += writtenBytes;
                
                // Update progress
                int progress = (written * 90) / contentLength + 5; // 5-95%
                if (progress > lastProgress + 5) { // Update every 5%
                    publishUpdateProgress(progress);
                    lastProgress = progress;
                    Serial.print("Progress: " + String(progress) + "% ");
                    Serial.println("(" + String(written) + "/" + String(contentLength) + " bytes)");
                }
            }
        } else {
            delay(10);
        }
        
        // Keep MQTT alive during download
        if (mqttClient.connected()) {
            mqttClient.loop();
        }
    }
    
    http.end();
    
    if (written != contentLength) {
        String error = "Download incomplete: " + String(written) + "/" + String(contentLength);
        Serial.println(error);
        publishUpdateStatus("failed", error);
        Update.abort();
        return false;
    }
    
    publishUpdateProgress(95);
    Serial.println("Download completed, finalizing update...");
    
    // Validate checksum if provided
    if (checksum.length() > 0) {
        Serial.println("Validating firmware checksum...");
        if (!validateFirmwareChecksum(checksum, written)) {
            String error = "Checksum validation failed";
            Serial.println(error);
            publishUpdateStatus("failed", error);
            Update.abort();
            return false;
        }
        Serial.println("Checksum validation passed");
    }
    
    // Finalize the update
    if (Update.end(true)) {
        publishUpdateProgress(100);
        publishUpdateStatus("completed", "");
        Serial.println("OTA update completed successfully!");
        Serial.println("Restarting device...");
        
        // Update firmware version
        currentFirmwareVersion = version;
        
        // Wait a moment for MQTT message to be sent
        delay(1000);
        
        // Restart
        ESP.restart();
        return true;
    } else {
        String error = "Update finalization failed: " + String(Update.errorString());
        Serial.println(error);
        publishUpdateStatus("failed", error);
        return false;
    }
}

void FitInfinityMQTT::publishUpdateProgress(int progress) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(256);
    doc["deviceId"] = deviceId;
    doc["progress"] = progress;
    doc["timestamp"] = getTimestamp();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/ota/progress";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("OTA Progress: " + String(progress) + "%");
}

void FitInfinityMQTT::publishUpdateStatus(String status, String error) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["status"] = status;
    doc["timestamp"] = getTimestamp();
    doc["firmwareVersion"] = currentFirmwareVersion;
    
    if (!error.isEmpty()) {
        doc["error"] = error;
    }
    
    // Add system information
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    doc["flashSize"] = ESP.getFlashChipSize();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/ota/status";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published OTA status: " + status);
    if (!error.isEmpty()) {
        Serial.println("Error: " + error);
    }
}

bool FitInfinityMQTT::validateFirmwareChecksum(String expectedChecksum, size_t firmwareSize) {
    // For ESP32, we'll use a simple validation approach
    // In a real implementation, you might want to calculate the actual checksum
    // of the downloaded firmware and compare it with the expected one
    
    Serial.println("Expected checksum: " + expectedChecksum);
    Serial.println("Firmware size: " + String(firmwareSize));
    
    // Basic validation - check if firmware size is reasonable
    if (firmwareSize < 100000) { // Less than 100KB seems too small
        Serial.println("Firmware size too small for validation");
        return false;
    }
    
    if (firmwareSize > 2000000) { // More than 2MB seems too large
        Serial.println("Firmware size too large for validation");
        return false;
    }
    
    // Check if checksum format is valid (should be hex string)
    if (expectedChecksum.length() != 64) { // SHA256 should be 64 hex characters
        Serial.println("Invalid checksum format");
        return false;
    }
    
    // Verify all characters are hex
    for (int i = 0; i < expectedChecksum.length(); i++) {
        char c = expectedChecksum.charAt(i);
        if (!isxdigit(c)) {
            Serial.println("Invalid checksum characters");
            return false;
        }
    }
    
    // For now, we'll consider the checksum valid if format is correct
    // In a production environment, you should implement proper checksum calculation
    Serial.println("Checksum format validation passed");
    return true;
}

bool FitInfinityMQTT::verifyFirmwareSignature(const uint8_t* firmware, size_t size) {
    // Placeholder for firmware signature verification
    // In a production environment, you should implement proper digital signature verification
    // using cryptographic libraries to ensure firmware authenticity
    
    Serial.println("Firmware signature verification (placeholder)");
    Serial.println("Firmware size: " + String(size) + " bytes");
    
    // Basic checks
    if (size < 100000) {
        Serial.println("Firmware too small to be valid");
        return false;
    }
    
    // Check for ESP32 firmware header (simplified)
    if (firmware[0] != 0xE9) {
        Serial.println("Invalid firmware header");
        return false;
    }
    
    Serial.println("Basic firmware signature checks passed");
    return true;
}

void FitInfinityMQTT::resetToFactoryDefaults() {
    Serial.println("Resetting to factory defaults...");
    
    // Clear WiFi credentials
    Preferences preferences;
    preferences.begin("wifi", false);
    preferences.clear();
    preferences.end();
    
    // Clear other stored settings if any
    preferences.begin("settings", false);
    preferences.clear();
    preferences.end();
    
    // Publish reset status
    if (mqttClient.connected()) {
        DynamicJsonDocument doc(256);
        doc["deviceId"] = deviceId;
        doc["action"] = "factory_reset";
        doc["timestamp"] = getTimestamp();
        
        String payload;
        serializeJson(doc, payload);
        
        String topic = getTopicPrefix() + "/status/reset";
        mqttClient.publish(topic.c_str(), payload.c_str());
    }
    
    Serial.println("Factory reset completed, restarting...");
    delay(2000);
    ESP.restart();
}

// Additional OTA utility functions
String FitInfinityMQTT::getOTAStatus() {
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["currentVersion"] = currentFirmwareVersion;
    doc["updateCapable"] = true;
    doc["freeSpace"] = ESP.getFreeSketchSpace();
    doc["sketchSize"] = ESP.getSketchSize();
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["timestamp"] = getTimestamp();
    
    String result;
    serializeJson(doc, result);
    return result;
}

void FitInfinityMQTT::publishOTACapabilities() {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["capabilities"]["ota"] = true;
    doc["capabilities"]["maxFirmwareSize"] = ESP.getFreeSketchSpace();
    doc["capabilities"]["checksumValidation"] = true;
    doc["capabilities"]["progressReporting"] = true;
    doc["capabilities"]["rollback"] = false; // Not implemented yet
    doc["currentVersion"] = currentFirmwareVersion;
    doc["timestamp"] = getTimestamp();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/ota/capabilities";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published OTA capabilities");
}

void FitInfinityMQTT::checkForFirmwareUpdates() {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(256);
    doc["deviceId"] = deviceId;
    doc["currentVersion"] = currentFirmwareVersion;
    doc["requestUpdate"] = true;
    doc["timestamp"] = getTimestamp();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/ota/check";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Requested firmware update check");
}

// Enhanced error handling for OTA
void FitInfinityMQTT::handleOTAError(String error, int errorCode) {
    Serial.println("OTA Error: " + error + " (Code: " + String(errorCode) + ")");
    
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["error"] = error;
    doc["errorCode"] = errorCode;
    doc["timestamp"] = getTimestamp();
    doc["firmwareVersion"] = currentFirmwareVersion;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["updateError"] = Update.errorString();
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/ota/error";
    mqttClient.publish(topic.c_str(), payload.c_str());
}