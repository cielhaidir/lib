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
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>FitInfinity WiFi Configuration</title>";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; }";
    html += ".container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #1877f2; text-align: center; margin-bottom: 30px; }";
    html += ".logo { text-align: center; margin-bottom: 20px; font-size: 24px; font-weight: bold; }";
    html += ".form-group { margin-bottom: 20px; }";
    html += "label { display: block; margin-bottom: 5px; color: #333; font-weight: bold; }";
    html += "input, select { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; box-sizing: border-box; }";
    html += "button { width: 100%; padding: 15px; background: #1877f2; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin-top: 10px; }";
    html += "button:hover { background: #166fe5; }";
    html += ".network-item { padding: 10px; border: 1px solid #eee; margin: 5px 0; border-radius: 5px; cursor: pointer; background: #f8f9fa; }";
    html += ".network-item:hover { background: #e9ecef; }";
    html += ".signal-strength { float: right; color: #666; }";
    html += ".status { text-align: center; margin: 20px 0; padding: 10px; border-radius: 5px; }";
    html += ".status.info { background: #d1ecf1; color: #0c5460; }";
    html += ".hidden { display: none; }";
    html += "</style></head><body>";
    html += "<div class=\"container\">";
    html += "<div class=\"logo\">🏋️ FitInfinity</div>";
    html += "<h1>WiFi Configuration</h1>";
    html += "<div class=\"status info\">";
    html += "Device ID: " + deviceId + "<br>";
    html += "Firmware: " + currentFirmwareVersion;
    html += "</div>";
    html += "<form action=\"/save\" method=\"POST\">";
    html += "<div class=\"form-group\">";
    html += "<label>Available Networks:</label>";
    html += "<button type=\"button\" onclick=\"scanNetworks()\" id=\"scanBtn\">Scan for Networks</button>";
    html += "<div id=\"networks\" class=\"hidden\"></div>";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label for=\"ssid\">Network Name (SSID):</label>";
    html += "<input type=\"text\" id=\"ssid\" name=\"ssid\" required placeholder=\"Enter WiFi network name\">";
    html += "</div>";
    html += "<div class=\"form-group\">";
    html += "<label for=\"password\">Password:</label>";
    html += "<input type=\"password\" id=\"password\" name=\"password\" placeholder=\"Enter WiFi password\">";
    html += "</div>";
    html += "<button type=\"submit\">Connect to WiFi</button>";
    html += "</form>";
    html += "<div class=\"status info\" style=\"margin-top: 30px;\">";
    html += "<strong>Instructions:</strong><br>";
    html += "1. Click \"Scan for Networks\" to see available WiFi networks<br>";
    html += "2. Select a network or enter manually<br>";
    html += "3. Enter the WiFi password<br>";
    html += "4. Click \"Connect to WiFi\"<br>";
    html += "5. The device will restart and connect to your network";
    html += "</div></div>";
    html += "<script>";
    html += "function selectNetwork(ssid, security) {";
    html += "document.getElementById('ssid').value = ssid;";
    html += "if (security !== 'Open') {";
    html += "document.getElementById('password').focus();";
    html += "}}";
    html += "function scanNetworks() {";
    html += "var btn = document.getElementById('scanBtn');";
    html += "var networksDiv = document.getElementById('networks');";
    html += "btn.textContent = 'Scanning...';";
    html += "btn.disabled = true;";
    html += "fetch('/scan').then(function(response) {";
    html += "return response.json();";
    html += "}).then(function(data) {";
    html += "networksDiv.innerHTML = '';";
    html += "networksDiv.classList.remove('hidden');";
    html += "if (data.networks && data.networks.length > 0) {";
    html += "data.networks.forEach(function(network) {";
    html += "var div = document.createElement('div');";
    html += "div.className = 'network-item';";
    html += "div.onclick = function() { selectNetwork(network.ssid, network.encryption); };";
    html += "var signalBars = getSignalBars(network.rssi);";
    html += "div.innerHTML = '<strong>' + network.ssid + '</strong>' +";
    html += "'<span class=\"signal-strength\">' + signalBars + ' ' + network.rssi + ' dBm</span>' +";
    html += "'<br><small>' + network.encryption + '</small>';";
    html += "networksDiv.appendChild(div);";
    html += "});";
    html += "} else {";
    html += "networksDiv.innerHTML = '<div class=\"status\">No networks found</div>';";
    html += "}";
    html += "}).catch(function(err) {";
    html += "console.error('Scan failed:', err);";
    html += "networksDiv.innerHTML = '<div class=\"status\">Scan failed. Please try again.</div>';";
    html += "}).finally(function() {";
    html += "btn.textContent = 'Scan for Networks';";
    html += "btn.disabled = false;";
    html += "});";
    html += "}";
    html += "function getSignalBars(rssi) {";
    html += "if (rssi > -50) return '📶';";
    html += "if (rssi > -60) return '📶';";
    html += "if (rssi > -70) return '📶';";
    html += "return '📶';";
    html += "}";
    html += "setTimeout(scanNetworks, 1000);";
    html += "</script></body></html>";
    
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
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>FitInfinity WiFi Configuration</title>";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 40px; background: #f0f2f5; text-align: center; }";
    html += ".container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += ".success { color: #28a745; font-size: 18px; margin-bottom: 20px; }";
    html += ".logo { font-size: 24px; font-weight: bold; margin-bottom: 20px; }";
    html += "</style></head><body>";
    html += "<div class=\"container\">";
    html += "<div class=\"logo\">🏋️ FitInfinity</div>";
    html += "<div class=\"success\">✅ WiFi Configuration Saved!</div>";
    html += "<p>The device will now restart and connect to your WiFi network.</p>";
    html += "<p><strong>Network:</strong> " + ssid + "</p>";
    html += "<p>Please wait for the device to reconnect...</p>";
    html += "</div>";
    html += "<script>";
    html += "setTimeout(function() { window.close(); }, 5000);";
    html += "</script></body></html>";
    
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