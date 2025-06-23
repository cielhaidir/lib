#ifndef FitInfinityMQTT_h
#define FitInfinityMQTT_h

#include "FitInfinityAPI.h"
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

class FitInfinityMQTT : public FitInfinityAPI {
private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    String deviceId;
    String mqttServer;
    int mqttPort;
    String mqttUsername;
    String mqttPassword;
    
    // OTA Update components
    HTTPClient otaClient;
    String currentFirmwareVersion;
    
    // WiFi Configuration
    WebServer* configServer;
    DNSServer* dnsServer;
    bool wifiConfigMode;
    
    // Callback function pointers
    void (*enrollmentCallback)(String employeeId, String employeeName, int fingerprintSlot);
    void (*firmwareUpdateCallback)(String version, String downloadUrl, String checksum);
    void (*modeChangeCallback)(bool enrollmentMode);
    void (*wifiConfigCallback)(String ssid, String password);

    // Internal state
    unsigned long lastHeartbeat;
    unsigned long lastReconnectAttempt;
    int reconnectAttempts;
    bool enrollmentMode;

public:
    FitInfinityMQTT(const char* baseUrl, const char* deviceId, const char* accessKey);
    
    // MQTT Connection Management
    bool connectMQTT(const char* server, int port, const char* username, const char* password);
    void mqttLoop();
    bool isMQTTConnected();
    void disconnectMQTT();
    
    // WiFi Management
    bool loadWifiCredentials(String& ssid, String& password);
    void saveWifiCredentials(const String& ssid, const String& password);
    bool connectWifi(const String& ssid, const String& password);
    bool startAccessPoint(const char* ssid, const char* password = "");
    void startConfigServer();
    void stopConfigServer();
    void scanWifiNetworks();
    void publishWifiScanResults(JsonArray networks);
    void handleWifiConfig(String ssid, String password);
    void subscribeWifiConfig();
    void publishWifiStatus(bool connected, String ssid, String ipAddress, String error = "");
    
    // Callback Registration
    void onEnrollmentRequest(void (*callback)(String, String, int));
    void onFirmwareUpdate(void (*callback)(String, String, String));
    void onModeChange(void (*callback)(bool));
    void onWifiConfig(void (*callback)(String, String));
    
    // Enrollment via MQTT
    void publishEnrollmentStatus(String employeeId, String status, int fingerprintId = -1);
    void setEnrollmentMode(bool enabled);
    
    // Real-time Attendance
    void publishAttendanceLog(String type, String id, String timestamp);
    void publishBulkAttendanceData(JsonArray attendanceData);
    
    // OTA Update System
    bool downloadAndInstallFirmware(String firmwareUrl, String version, String checksum);
    void publishUpdateProgress(int progress);
    void publishUpdateStatus(String status, String error = "");
    bool validateFirmwareChecksum(String checksum, size_t firmwareSize);
    String getOTAStatus();
    void publishOTACapabilities();
    void checkForFirmwareUpdates();
    void handleOTAError(String error, int errorCode);
    
    // Device Management
    void publishHeartbeat();
    void publishDeviceStatus(String status);
    void publishDeviceError(String error);
    void publishDeviceMetrics();
    
    // System Information
    String getDeviceInfo();
    String getFirmwareVersion();
    void setFirmwareVersion(String version);
    float getTemperature();
    int getSignalStrength();
    long getUptime();
    int getFreeHeap();
    
private:
    String getTopicPrefix();
    void setupSubscriptions();
    void handleMqttMessage(char* topic, byte* payload, unsigned int length);
    bool reconnectMQTT();
    void sendHeartbeat();
    bool verifyFirmwareSignature(const uint8_t* firmware, size_t size);
    void resetToFactoryDefaults();
    
    // WiFi Configuration Portal
    void handleWifiConfigPortal();
    void serveConfigPage();
    void handleWifiScan();
    void handleWifiSave();
    void handleConfigRoot();
    void handleNotFound();
};

#endif