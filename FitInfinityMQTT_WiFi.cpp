#include "FitInfinityMQTT.h"

// WiFi Management Functions

bool FitInfinityMQTT::loadWifiCredentials(String& ssid, String& password) {
    Preferences preferences;
    preferences.begin("wifi", true);
    
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    
    preferences.end();
    
    return (ssid.length() > 0);
}

void FitInfinityMQTT::saveWifiCredentials(const String& ssid, const String& password) {
    Preferences preferences;
    preferences.begin("wifi", false);
    
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    
    preferences.end();
    
    Serial.println("WiFi credentials saved: " + ssid);
}

bool FitInfinityMQTT::connectWifi(const String& ssid, const String& password) {
    Serial.println("Connecting to WiFi: " + ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected successfully!");
        Serial.println("IP address: " + WiFi.localIP().toString());
        Serial.println("Signal strength: " + String(WiFi.RSSI()) + " dBm");
        
        // Publish WiFi status via MQTT if connected
        if (mqttClient.connected()) {
            publishWifiStatus(true, ssid, WiFi.localIP().toString());
        }
        
        return true;
    } else {
        Serial.println("WiFi connection failed!");
        
        // Publish WiFi status via MQTT if connected
        if (mqttClient.connected()) {
            publishWifiStatus(false, ssid, "", "Connection timeout");
        }
        
        return false;
    }
}

bool FitInfinityMQTT::startAccessPoint(const char* ssid, const char* password) {
    Serial.println("Starting Access Point: " + String(ssid));
    
    WiFi.mode(WIFI_AP);
    bool success = WiFi.softAP(ssid, password);
    
    if (success) {
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        
        Serial.println("Access Point started successfully!");
        Serial.println("AP IP address: " + WiFi.softAPIP().toString());
        Serial.println("AP SSID: " + String(ssid));
        
        wifiConfigMode = true;
        return true;
    } else {
        Serial.println("Failed to start Access Point!");
        return false;
    }
}

void FitInfinityMQTT::startConfigServer() {
    if (configServer) {
        delete configServer;
    }
    if (dnsServer) {
        delete dnsServer;
    }
    
    configServer = new WebServer(80);
    dnsServer = new DNSServer();
    
    // Setup captive portal
    dnsServer->start(53, "*", WiFi.softAPIP());
    
    // Configure web server routes
    configServer->on("/", HTTP_GET, [this]() { handleConfigRoot(); });
    configServer->on("/scan", HTTP_GET, [this]() { handleWifiScan(); });
    configServer->on("/save", HTTP_POST, [this]() { handleWifiSave(); });
    configServer->onNotFound([this]() { handleNotFound(); });
    
    configServer->begin();
    Serial.println("WiFi configuration server started on http://192.168.4.1");
}

void FitInfinityMQTT::stopConfigServer() {
    if (configServer) {
        configServer->stop();
        delete configServer;
        configServer = nullptr;
    }
    if (dnsServer) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    
    wifiConfigMode = false;
    Serial.println("WiFi configuration server stopped");
}

void FitInfinityMQTT::handleConfigRoot() {
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>FitInfinity WiFi Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; }
        .container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #1877f2; text-align: center; margin-bottom: 30px; }
        .logo { text-align: center; margin-bottom: 20px; font-size: 24px; font-weight: bold; }
        .form-group { margin-bottom: 20px; }
        label { display: block; margin-bottom: 5px; color: #333; font-weight: bold; }
        input, select { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; box-sizing: border-box; }
        button { width: 100%; padding: 15px; background: #1877f2; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin-top: 10px; }
        button:hover { background: #166fe5; }
        .network-item { padding: 10px; border: 1px solid #eee; margin: 5px 0; border-radius: 5px; cursor: pointer; background: #f8f9fa; }
        .network-item:hover { background: #e9ecef; }
        .signal-strength { float: right; color: #666; }
        .status { text-align: center; margin: 20px 0; padding: 10px; border-radius: 5px; }
        .status.info { background: #d1ecf1; color: #0c5460; }
        .hidden { display: none; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">🏋️ FitInfinity</div>
        <h1>WiFi Configuration</h1>
        <div class="status info">
            Device ID: )" + deviceId + R"(<br>
            Firmware: )" + currentFirmwareVersion + R"(
        </div>
        
        <form action="/save" method="POST">
            <div class="form-group">
                <label>Available Networks:</label>
                <button type="button" onclick="scanNetworks()" id="scanBtn">Scan for Networks</button>
                <div id="networks" class="hidden"></div>
            </div>
            
            <div class="form-group">
                <label for="ssid">Network Name (SSID):</label>
                <input type="text" id="ssid" name="ssid" required placeholder="Enter WiFi network name">
            </div>
            
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password" placeholder="Enter WiFi password">
            </div>
            
            <button type="submit">Connect to WiFi</button>
        </form>
        
        <div class="status info" style="margin-top: 30px;">
            <strong>Instructions:</strong><br>
            1. Click "Scan for Networks" to see available WiFi networks<br>
            2. Select a network or enter manually<br>
            3. Enter the WiFi password<br>
            4. Click "Connect to WiFi"<br>
            5. The device will restart and connect to your network
        </div>
    </div>
    
    <script>
        function selectNetwork(ssid, security) {
            document.getElementById('ssid').value = ssid;
            // Focus on password field if network is secured
            if (security !== 'Open') {
                document.getElementById('password').focus();
            }
        }
        
        function scanNetworks() {
            const btn = document.getElementById('scanBtn');
            const networksDiv = document.getElementById('networks');
            
            btn.textContent = 'Scanning...';
            btn.disabled = true;
            
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    networksDiv.innerHTML = '';
                    networksDiv.classList.remove('hidden');
                    
                    if (data.networks && data.networks.length > 0) {
                        data.networks.forEach(network => {
                            const div = document.createElement('div');
                            div.className = 'network-item';
                            div.onclick = () => selectNetwork(network.ssid, network.encryption);
                            
                            const signalBars = getSignalBars(network.rssi);
                            div.innerHTML = `
                                <strong>${network.ssid}</strong>
                                <span class="signal-strength">${signalBars} ${network.rssi} dBm</span>
                                <br><small>${network.encryption}</small>
                            `;
                            networksDiv.appendChild(div);
                        });
                    } else {
                        networksDiv.innerHTML = '<div class="status">No networks found</div>';
                    }
                })
                .catch(err => {
                    console.error('Scan failed:', err);
                    networksDiv.innerHTML = '<div class="status">Scan failed. Please try again.</div>';
                })
                .finally(() => {
                    btn.textContent = 'Scan for Networks';
                    btn.disabled = false;
                });
        }
        
        function getSignalBars(rssi) {
            if (rssi > -50) return '📶';
            if (rssi > -60) return '📶';
            if (rssi > -70) return '📶';
            return '📶';
        }
        
        // Auto-scan on page load
        setTimeout(scanNetworks, 1000);
    </script>
</body>
</html>
    )";
    
    configServer->send(200, "text/html", html);
}

void FitInfinityMQTT::handleWifiScan() {
    Serial.println("WiFi scan requested via web interface");
    
    int networkCount = WiFi.scanNetworks();
    
    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray("networks");
    
    for (int i = 0; i < networkCount; i++) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
    }
    
    String response;
    serializeJson(doc, response);
    
    configServer->send(200, "application/json", response);
    
    Serial.println("WiFi scan completed, found " + String(networkCount) + " networks");
}

void FitInfinityMQTT::handleWifiSave() {
    String ssid = configServer->arg("ssid");
    String password = configServer->arg("password");
    
    Serial.println("Received WiFi configuration:");
    Serial.println("SSID: " + ssid);
    
    // Save credentials
    saveWifiCredentials(ssid, password);
    
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>FitInfinity WiFi Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; background: #f0f2f5; text-align: center; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .success { color: #28a745; font-size: 18px; margin-bottom: 20px; }
        .logo { font-size: 24px; font-weight: bold; margin-bottom: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">🏋️ FitInfinity</div>
        <div class="success">✅ WiFi Configuration Saved!</div>
        <p>The device will now restart and connect to your WiFi network.</p>
        <p><strong>Network:</strong> )" + ssid + R"(</p>
        <p>Please wait for the device to reconnect...</p>
    </div>
    <script>
        setTimeout(() => {
            window.close();
        }, 5000);
    </script>
</body>
</html>
    )";
    
    configServer->send(200, "text/html", html);
    
    // Delay and restart
    delay(2000);
    ESP.restart();
}

void FitInfinityMQTT::handleNotFound() {
    // Redirect to config page for captive portal
    configServer->sendHeader("Location", "/", true);
    configServer->send(302, "text/plain", "");
}

void FitInfinityMQTT::scanWifiNetworks() {
    Serial.println("Scanning for WiFi networks...");
    
    int networkCount = WiFi.scanNetworks();
    
    DynamicJsonDocument doc(2048);
    JsonArray networks = doc.createNestedArray("networks");
    
    for (int i = 0; i < networkCount; i++) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
    }
    
    publishWifiScanResults(networks);
    
    Serial.println("WiFi scan completed, found " + String(networkCount) + " networks");
}

void FitInfinityMQTT::publishWifiScanResults(JsonArray networks) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(2048);
    doc["deviceId"] = deviceId;
    doc["networks"] = networks;
    doc["timestamp"] = getTimestamp();
    doc["action"] = "scan";
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/config/wifi/request";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published WiFi scan results");
}

void FitInfinityMQTT::handleWifiConfig(String ssid, String password) {
    Serial.println("Handling WiFi configuration via MQTT:");
    Serial.println("SSID: " + ssid);
    
    // Save credentials
    saveWifiCredentials(ssid, password);
    
    // Publish status update
    publishWifiStatus(false, ssid, "", "Configuring...");
    
    // Restart to connect with new credentials
    delay(1000);
    ESP.restart();
}

void FitInfinityMQTT::publishWifiStatus(bool connected, String ssid, String ipAddress, String error) {
    if (!mqttClient.connected()) return;
    
    DynamicJsonDocument doc(512);
    doc["deviceId"] = deviceId;
    doc["connected"] = connected;
    doc["ssid"] = ssid;
    doc["ipAddress"] = ipAddress;
    doc["timestamp"] = getTimestamp();
    doc["action"] = "status";
    
    if (!error.isEmpty()) {
        doc["error"] = error;
    }
    
    if (connected) {
        doc["rssi"] = WiFi.RSSI();
        doc["macAddress"] = WiFi.macAddress();
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String topic = getTopicPrefix() + "/config/wifi/status";
    mqttClient.publish(topic.c_str(), payload.c_str());
    
    Serial.println("Published WiFi status: " + String(connected ? "connected" : "disconnected"));
}

void FitInfinityMQTT::subscribeWifiConfig() {
    if (!mqttClient.connected()) return;
    
    String topic = getTopicPrefix() + "/config/wifi/response";
    mqttClient.subscribe(topic.c_str());
    
    Serial.println("Subscribed to WiFi configuration updates");
}