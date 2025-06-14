#ifndef FitInfinityAPI_h
#define FitInfinityAPI_h

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <Adafruit_Fingerprint.h>

class FitInfinityAPI {
  public:
    FitInfinityAPI(const char* baseUrl, const char* deviceId, const char* accessKey);
    
    // Initialize with WiFi credentials
    bool begin(const char* ssid, const char* password, int8_t sdCardPin = -1);
    
    // Authentication
    bool authenticate();
    
    // Attendance logging
    bool logFingerprint(int fingerId);
    bool logRFID(const char* rfidNumber);
    
    // Enrollment methods
    bool getPendingEnrollments(JsonArray& result);
    bool beginFingerprint(Stream* stream);
    bool enrollFingerprint(int id);
    bool updateEnrollmentStatus(const char* employeeId, int fingerprintId, bool success);
    uint8_t scanFingerprint(int* fingerprintId);
    
    // Offline storage
    void storeOfflineRecord(const char* type, const char* id, const char* timestamp);
    bool syncOfflineRecords();
    void setOfflineStorageMode(bool useSD);
    bool isSDCardEnabled();
    String getOfflineStorageStats();
    
    // Status methods
    bool isConnected();
    String getLastError();
    int getLastResponseCode();
    
    // Helper functions
    String getTimestamp();
    void setNTPServer(const char* server);
    void setTimeout(uint16_t timeoutMs);

  private:
    // Configuration
    String _baseUrl;
    Adafruit_Fingerprint* _fingerSensor;
    String _deviceId;
    String _accessKey;
    String _ntpServer;
    uint16_t _timeout;
    
    // State
    bool _isConnected;
    String _lastError;
    int _lastResponseCode;
    bool _useSDCard;
    int8_t _sdCardPin;
    
    // Offline storage paths
    static const char* OFFLINE_FILE;
    static const char* OFFLINE_TEMP;
    
    // Internal methods
    bool makeRequest(const char* action, JsonDocument& doc);
    void updateConnectionStatus();
    void initTimeSync();
    bool initSDCard();
    
    // SD Card operations
    bool writeToSDCard(const char* type, const char* id, const char* timestamp);
    bool readFromSDCard(JsonArray& records);
    bool clearProcessedRecords(int count);
    bool renameFile(const char* from, const char* to);
};

#endif