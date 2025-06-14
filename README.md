# FitInfinity ESP32 API Library

An Arduino library for ESP32 devices to interact with the FitInfinity attendance system API. This library handles employee fingerprint and member RFID attendance logging with offline storage support.

## Features

- Easy WiFi connection management
- Device authentication via REST API
- Employee fingerprint enrollment and management
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
  * `getPendingEnrollments` - Get list of pending fingerprint enrollments

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
    "https://your-fitinfinity-domain.com",  // Base URL of your Next.js application
    "your_device_id",                       // Device identifier
    "your_access_key"                       // Access key for authentication
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
    // Initialize fingerprint sensor (using Software Serial)
    SoftwareSerial fingerSerial(D3, D4); // RX, TX
    api.beginFingerprint(&fingerSerial);

    // Check for pending enrollments
    DynamicJsonDocument doc(1024);
    JsonArray enrollments = doc.to<JsonArray>();
    if (api.getPendingEnrollments(enrollments)) {
        for (JsonObject enrollment : enrollments) {
            const char* id = enrollment["id"];
            const char* nama = enrollment["nama"];
            int fingerId = enrollment["finger_id"].as<int>();
            
            Serial.printf("Enrolling fingerprint for %s (ID: %s)\n", nama, id);
            
            // Perform enrollment
            bool success = api.enrollFingerprint(fingerId);
            if (success) {
                Serial.println("Enrollment successful!");
                // Update enrollment status
                api.updateEnrollmentStatus(id, fingerId, true);
            } else {
                Serial.printf("Enrollment failed: %s\n", api.getLastError());
                api.updateEnrollmentStatus(id, fingerId, false);
            }
        }
    }
    
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
// Set custom NTP server (optional)
api.setNTPServer("pool.ntp.org");  // Default: time.google.com

// Set custom timeout for API requests (optional)
api.setTimeout(10000);  // Default: 5000ms

// Check connection status
if (api.isConnected()) {
    Serial.println("Device is online");
}
```

The library includes automatic time synchronization using NTP. This ensures accurate timestamps for attendance records, even when operating offline.

## Fingerprint Operations

### Enrollment

The library supports employee fingerprint enrollment using the Adafruit Fingerprint sensor:

```cpp
// Initialize fingerprint sensor
SoftwareSerial fingerSerial(D3, D4); // RX, TX pins
if (api.beginFingerprint(&fingerSerial)) {
    Serial.println("Fingerprint sensor ready");
}

// Enroll new fingerprint
bool success = api.enrollFingerprint(fingerId);
if (success) {
    Serial.println("Fingerprint enrolled successfully");
} else {
    Serial.println("Error: " + api.getLastError());
}

// Update enrollment status on server
api.updateEnrollmentStatus(employeeId, fingerId, success);
```

The enrollment process:
1. `getPendingEnrollments()` checks for pending enrollment requests from server
2. For each pending enrollment:
   - System prompts user to place finger on sensor
   - Two scans are required for verification
   - `enrollFingerprint()` handles the complete enrollment workflow
3. Status is updated on server using `updateEnrollmentStatus()`
4. Process repeats for any remaining enrollments

### Scanning and Verification

The library provides fingerprint scanning and verification:

```cpp
// Initialize sensor first
SoftwareSerial fingerSerial(D3, D4);
api.beginFingerprint(&fingerSerial);

// Scan for fingerprint
int fingerprintId;
uint8_t result = api.scanFingerprint(&fingerprintId);

switch (result) {
    case FINGERPRINT_OK:
        Serial.printf("Found fingerprint ID: %d\n", fingerprintId);
        // Log attendance
        if (api.logFingerprint(fingerprintId)) {
            Serial.println("Attendance logged!");
        }
        break;
    case FINGERPRINT_NOTFOUND:
        Serial.println("No matching fingerprint found");
        break;
    case FINGERPRINT_NOFINGER:
        // No finger detected, normal scanning state
        break;
    case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        break;
    case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        break;
    case FINGERPRINT_FEATUREFAIL:
        Serial.println("Could not find fingerprint features");
        break;
    default:
        Serial.println("Error: " + api.getLastError());
        break;
}
```

The `scanFingerprint()` method:
- Returns FINGERPRINT_OK when a match is found
- Sets the fingerprintId parameter to the matched ID
- Returns other status codes for different conditions (no finger, no match, etc.)

## API Request/Response Format

### Authentication Request
```json
{
  "action": "authenticate",
  "deviceId": "your_device_id",
  "accessKey": "your_access_key"
}
```

### Attendance Log Request
```json
{
  "action": "logFingerprint",  // or "logRFID"
  "deviceId": "your_device_id",
  "accessKey": "your_access_key",
  "fingerId": 123,             // or "rfid": "card_number"
  "timestamp": "2025-06-14T15:54:35.000Z"
}
```

### Bulk Sync Request (Offline Records)
```json
{
  "action": "bulkLog",
  "deviceId": "your_device_id",
  "accessKey": "your_access_key",
  "records": [
    {
      "type": "fingerprint",
      "id": "123",
      "timestamp": "2025-06-14T15:54:35.000Z"
    }
  ]
}
```

### Error Response Format
```json
{
  "error": "Error message details"
}
```

## Error Handling

```cpp
// Check for errors after operations
if (!api.logFingerprint(fingerprintId)) {
    Serial.println("Error: " + api.getLastError());
    Serial.println("Response code: " + String(api.getLastResponseCode()));
}

// Fingerprint operation status codes
switch (result) {
    case FINGERPRINT_OK:          // Operation successful
    case FINGERPRINT_NOFINGER:    // No finger detected
    case FINGERPRINT_NOTFOUND:    // Fingerprint not found in database
    case FINGERPRINT_PACKETRECIEVEERR: // Communication error
    case FINGERPRINT_IMAGEFAIL:   // Failed to capture image
    case FINGERPRINT_FEATUREFAIL: // Failed to extract features
}
```

## Offline Storage

The library provides two storage options for offline records:

### 1. SD Card Storage
```cpp
// Enable SD card storage by providing the CS pin
api.begin(wifi_ssid, wifi_password, SD_CS_PIN);

// Initialize SD card storage
if (api.isSDCardEnabled()) {
    Serial.println("Using SD card storage");
}

// Offline Storage Behavior
// 1. Records are automatically saved when offline
// 2. Each record includes type, ID, and timestamp
// 3. Records are stored in JSON format
// 4. Sync process:
    // - Syncs up to 50 records per batch
    // - Removes successfully synced records
    // - Preserves failed records for retry
    // - Automatic retry on next connection

// Sync Process Example
if (api.isConnected()) {
    if (api.syncOfflineRecords()) {
        Serial.println("Sync batch complete");
        // Will automatically process remaining records
        // in subsequent sync calls
    } else {
        Serial.println("Sync error: " + api.getLastError());
    }
}

// Storage Statistics
String stats = api.getOfflineStorageStats();
Serial.println(stats);
// Example outputs:
// "Storage: SD Card, Records: 42, Space: 1.2MB"
// "Storage: Memory, Records: 15/100"
```

### 2. Memory Storage (Fallback)
When SD card storage is not available or disabled, the library automatically uses memory-based storage with the following characteristics:

- Stores up to 100 most recent records in a ring buffer
- Automatically removes oldest records when limit is reached
- Records persist through device resets until successfully synced
- Thread-safe implementation for reliable storage

```cpp
// Get current storage stats
String stats = api.getOfflineStorageStats();
Serial.println(stats);  // Shows record count and storage mode

// Force memory storage mode
api.setOfflineStorageMode(false);  // false = memory storage
```

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