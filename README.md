# FitInfinity ESP32 API Library

An Arduino library for ESP32 devices to interact with the FitInfinity attendance system API. This library handles employee fingerprint and member RFID attendance logging with offline storage support.

## Features

- Easy WiFi connection management
- Device authentication via REST API
- Employee fingerprint attendance logging
- Member RFID attendance logging
- Flexible offline storage:
  * SD card storage (optional)
  * Fallback to memory storage
- Automatic record syncing
- NTP time synchronization
- Error handling with JSON response parsing
- All API requests include an `action` parameter:
  * `authenticate` - Device authentication
  * `logFingerprint` - Log fingerprint attendance
  * `logRFID` - Log RFID card attendance
  * `bulkLog` - Sync offline records

## Installation

1. Download the library as a ZIP file
2. In Arduino IDE: Sketch -> Include Library -> Add .ZIP Library
3. Select the downloaded ZIP file
4. Install required dependencies:
   - ArduinoJson (>= 6.0.0)
   - WiFi

## Usage

```cpp
#include <FitInfinityAPI.h>

// Initialize the API client
FitInfinityAPI api(
    "https://your-api-domain.com/api/esp32",  // New REST API endpoint
    "your_device_id",
    "your_access_key"
);

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi and initialize with optional SD card
    // Use -1 for sdCardPin to disable SD card usage
    if (api.begin("wifi_ssid", "wifi_password", 5)) {  // 5 is the SD card CS pin
        Serial.println("Connected!");
        
        if (api.isSDCardEnabled()) {
            Serial.println("SD Card ready for offline storage");
        }
    }
}

void loop() {
    // Log employee fingerprint attendance
    if (fingerprintDetected) {
        if (api.logFingerprint(fingerprintId)) {
            Serial.println("Fingerprint logged!");
        }
    }
    
    // Log member RFID attendance
    if (rfidDetected) {
        if (api.logRFID(rfidNumber)) {
            Serial.println("RFID logged!");
        }
    }
    
    // Sync any stored offline records
    if (api.isConnected()) {
        api.syncOfflineRecords();
    }
}
```

## Configuration Options

```cpp
// Set custom NTP server
api.setNTPServer("pool.ntp.org");

// Set custom timeout (milliseconds)
api.setTimeout(10000);
```

## Error Handling

```cpp
// Request payload format:
// {
//   "action": "logFingerprint",  // or other actions
//   "deviceId": "your_device_id",
//   "accessKey": "your_access_key",
//   ... other parameters
// }

if (!api.logFingerprint(fingerprintId)) {
    Serial.println("Error: " + api.getLastError());
    Serial.println("Response code: " + String(api.getLastResponseCode()));
}
```

## Offline Storage

The library provides two storage options for offline records:

### 1. SD Card Storage
```cpp
// Enable SD card storage by providing the CS pin
api.begin(wifi_ssid, wifi_password, SD_CS_PIN);

// Check if SD card is working
if (api.isSDCardEnabled()) {
    Serial.println("Using SD card storage");
}

// Get storage statistics
Serial.println(api.getOfflineStorageStats());
```

### 2. Memory Storage (Fallback)
If SD card is not available or disabled, the library automatically falls back to memory-based storage. Memory storage is limited to the last 100 records.

### Storage Management
```cpp
// Manually change storage mode
api.setOfflineStorageMode(true);  // true for SD card, false for memory

// Sync stored records
if (api.syncOfflineRecords()) {
    Serial.println("Records synced successfully");
}
```

## Example Sketches

Check the `examples` folder for complete implementation examples:
- `BasicAttendance`: Simple attendance logging
- `OfflineStorage`: Demonstrates SD card and memory-based storage
- `CompleteSystem`: Full implementation with LCD display and error handling

### Running the OfflineStorage Example
1. Wire up the SD card module to your ESP32:
   - CS: GPIO 5 (configurable)
   - MOSI: GPIO 23
   - MISO: GPIO 19
   - SCK: GPIO 18
2. Update WiFi and API credentials
3. Upload the sketch
4. Use Serial Monitor commands:
   - "F123" - Log fingerprint ID 123
   - "R456" - Log RFID number 456
   - "SYNC" - Sync offline records
   - "STATS" - Show storage statistics

## Contributing

Please report any issues or feature requests in the GitHub repository issue tracker.

## License

This library is released under the MIT License. See the LICENSE file for details.