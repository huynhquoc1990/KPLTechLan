#include "WiFiManager.h"
#include "Webservice.h"
#include <ArduinoJson.h>
// #include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

WiFiManager::WiFiManager(AsyncWebServer *webServer) 
    : server(webServer), isLoggedIn(false) {
    currentConfig.isValid = false;
}

WiFiManager::~WiFiManager() {
    if (server) {
        server->end();
    }
}

bool WiFiManager::checkWiFiConfig() {
    return loadConfig() && currentConfig.isValid;
}

bool WiFiManager::isRouterAvailable() {
    if (!currentConfig.isValid) {
        return false;
    }
    
    // Quick scan for router SSID (non-blocking, async scan)
    // Note: We use a quick scan to check if router is broadcasting
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow disconnect to complete
    
    // Start async scan (non-blocking)
    int scanResult = WiFi.scanNetworks(true, false); // true = async, false = don't show hidden
    
    // Wait for scan to complete (max 5 seconds)
    int attempts = 0;
    while (WiFi.scanComplete() < 0 && attempts < 50) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        attempts++;
    }
    
    int n = WiFi.scanComplete();
    if (n < 0) {
        // Scan didn't complete in time
        WiFi.scanDelete();
        return false;
    }
    
    // Check if our SSID is in the scan results
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == currentConfig.ssid) {
            found = true;
            Serial.printf("[Router Check] Found router SSID: %s (RSSI: %d dBm)\n", 
                         currentConfig.ssid.c_str(), WiFi.RSSI(i));
            break;
        }
    }
    
    WiFi.scanDelete();
    return found;
}

bool WiFiManager::connectToWiFi() {
    if (!currentConfig.isValid) {
        Serial.println("No valid WiFi configuration");
        return false;
    }
    
    Serial.printf("Connecting to WiFi: %s\n", currentConfig.ssid.c_str());
    
    // Prepare STA connection
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);                 // save power while connecting
    WiFi.setTxPower(WIFI_POWER_8_5dBm);  // moderate power
    WiFi.begin(currentConfig.ssid.c_str(), currentConfig.password.c_str());
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Increased timeout: 60 attempts × 500ms = 30 seconds
    // This gives router more time to boot up after power loss
    int attempts = 0;
    const int maxAttempts = 60; // Increased from 30 to 60
    
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
        if (attempts % 10 == 0) { // Print every 5 seconds
            Serial.print(".");
        }
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // Keep modem sleep enabled to reduce temperature/power
        WiFi.setSleep(true);
        // Keep TX power low unless signal is weak
        WiFi.setTxPower(WIFI_POWER_5dBm);
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("\nWiFi connection failed");
        return false;
    }
}

void WiFiManager::startWiFiAP() {
    Serial.println("[AP] Disconnecting from any existing WiFi network.");
    WiFi.disconnect(true, true); // Xóa cấu hình cũ và ngắt kết nối
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));

    Serial.println("[AP] Starting Access Point...");
    // Prefer AP+STA to allow scanning without dropping AP
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false); // keep AP stable
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4); // channel 1, hidden=0, max 4 clients
    // Note: Adjusting AP beacon interval is not available in Arduino API
    Serial.printf("AP started: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::startConfigurationPortal() {
    // Reduce CPU while in configuration to lower heat
    setCpuFrequencyMhz(80);
    // Inform system we're in config portal for thermal control
    extern bool inConfigPortal;
    inConfigPortal = true;
    startWiFiAP();
    scanWiFi(); // Quét một lần duy nhất ở đây
    // Ensure AP is up after scan (redundant safety)
    if (WiFi.getMode() != WIFI_MODE_AP) {
        Serial.println("[AP] Safety check: AP not active, starting again...");
        startWiFiAP();
    }
    setupWebServer();
    Serial.println("Configuration portal is active.");
}

void WiFiManager::scanWiFi() {
    Serial.println("\n[SCAN] Starting WiFi scan...");
    
    Serial.println("[SCAN] Switching to AP+STA for scanning (keep AP alive).");
    WiFi.mode(WIFI_AP_STA);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow time for mode change
    
    Serial.println("[SCAN] Deleting previous scan results.");
    WiFi.scanDelete();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    Serial.println("[SCAN] Starting synchronous network scan...");
    int scanResult = WiFi.scanNetworks(false, true); // false = sync, true = show hidden
    
    Serial.printf("[SCAN] WiFi.scanNetworks() returned: %d\n", scanResult);

    if (scanResult < 0) {
        Serial.printf("[SCAN] ERROR: Scan failed with error code: %d\n", scanResult);
        wifiList = "[]";
        // Restore AP mode
        WiFi.mode(WIFI_AP);
        return;
    }
    
    Serial.printf("[SCAN] Scan completed successfully. Found %d networks.\n", scanResult);
    
    wifiList = "[";
    for (int i = 0; i < scanResult; i++) {
        if (i > 0) wifiList += ",";
        wifiList += "\"" + WiFi.SSID(i) + "\"";
        Serial.printf("  > Network %d: %s (%ddBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
    wifiList += "]";
    
    WiFi.scanDelete();
    
    // Ensure AP is configured after scanning
    Serial.println("[SCAN] Ensuring AP is active after scan...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
    Serial.printf("[SCAN] AP re-started: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    Serial.println("[SCAN] WiFi scan completed successfully.");
}

void WiFiManager::setupWebServer() {
    if (!server) return;
    
    setupRoutes();
    server->begin();
    Serial.println("Web server started");
}

void WiFiManager::startWebServer() {
    if (server) {
        server->begin();
    }
}

void WiFiManager::stopWebServer() {
    if (server) {
        server->end();
    }
}

bool WiFiManager::loadConfig() {
    String configData = readFileConfig("/config.txt");
    if (configData.isEmpty()) {
        currentConfig.isValid = false;
        return false;
    }
    
    int split1 = configData.indexOf('\n');
    int split2 = configData.lastIndexOf('\n');
    
    if (split1 == -1 || split2 == -1 || split1 == split2) {
        currentConfig.isValid = false;
        return false;
    }
    
    currentConfig.ssid = configData.substring(0, split1);
    currentConfig.password = configData.substring(split1 + 1, split2);
    currentConfig.topic = configData.substring(split2 + 1);
    
    currentConfig.isValid = validateConfig(currentConfig.ssid, currentConfig.password, currentConfig.topic);
    
    if (currentConfig.isValid) {
        // Update global TopicMqtt with loaded ID
        strncpy(TopicMqtt, currentConfig.topic.c_str(), sizeof(TopicMqtt) - 1);
        TopicMqtt[sizeof(TopicMqtt) - 1] = '\0'; // Ensure null termination
        
        Serial.printf("Loaded config: SSID=%s, Topic=%s\n", 
                     currentConfig.ssid.c_str(), currentConfig.topic.c_str());
    }
    
    return currentConfig.isValid;
}

bool WiFiManager::saveConfig(const String &ssid, const String &password, const String &topic) {
    // Use topic parameter from web form
    if (!validateConfig(ssid, password, topic)) {
        return false;
    }
    
    String configData = ssid + "\n" + password + "\n" + topic;
    writeFileConfig("/config.txt", configData);
    
    // Update current config
    currentConfig.ssid = ssid;
    currentConfig.password = password;
    currentConfig.topic = topic;
    currentConfig.isValid = true;
    
    // Update global TopicMqtt with the new ID from form
    strncpy(TopicMqtt, topic.c_str(), sizeof(TopicMqtt) - 1);
    TopicMqtt[sizeof(TopicMqtt) - 1] = '\0'; // Ensure null termination
    
    Serial.printf("WiFi configuration saved with Topic ID: %s\n", TopicMqtt);
    return true;
}

bool WiFiManager::resetConfig() {
    if (LittleFS.exists("/config.txt")) {
        return LittleFS.remove("/config.txt");
    }
    return true;
}

void WiFiManager::setupRoutes() {
    if (!server) return;
    
    // Root route
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleRoot(request);
    });
    
    // Login routes
    server->on("/login", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", loginPage);
    });
    
    server->on("/login", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLogin(request);
    });
    
    server->on("/logout", HTTP_GET, [this](AsyncWebServerRequest *request) {
        isLoggedIn = false;
        request->redirect("/login");
    });
    
    // Config routes
    server->on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleConfig(request);
    });
    
    server->on("/wifi_scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleWiFiScan(request);
    });
    
    server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSaveConfig(request);
    });
    
    server->on("/reset_config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleResetConfig(request);
    });
    
    // API routes
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleAPIStatus(request);
    });
}

void WiFiManager::handleRoot(AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
        request->redirect("/login");
    } else {
        request->redirect("/config");
    }
}

void WiFiManager::handleLogin(AsyncWebServerRequest *request) {
    if (request->hasParam("username", true) && request->hasParam("password", true)) {
        String username = request->getParam("username", true)->value();
        String password = request->getParam("password", true)->value();
        
        if (username == adminUser && password == adminPass) {
            isLoggedIn = true;
            request->redirect("/config");
        } else {
            request->send(200, "text/html", loginFail);
        }
    }
}

void WiFiManager::handleConfig(AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
        request->redirect("/login");
        return;
    }
    request->send(200, "text/html", configPage1);
}

void WiFiManager::handleWiFiScan(AsyncWebServerRequest *request) {
    // Không cần quét lại, chỉ trả về danh sách đã lưu
    request->send(200, "application/json", wifiList);
}

void WiFiManager::handleSaveConfig(AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
        request->redirect("/login");
        return;
    }
    
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
        String ssid = request->getParam("ssid", true)->value();
        String password = request->getParam("password", true)->value();
        String topic = request->getParam("id", true)->value();
        
        if (saveConfig(ssid, password, topic)) {
            request->send(200, "text/html", result);
            delay(2000); // Đợi để client nhận được response
            ESP.restart(); // Khởi động lại ở đây
        } else {
            request->send(200, "text/html", serverFail);
        }
    }
}

void WiFiManager::handleResetConfig(AsyncWebServerRequest *request) {
    if (!isLoggedIn) {
        request->redirect("/login");
        return;
    }
    
    resetConfig();
    // Không cần scan lại vì AP mode đang hoạt động và đã có danh sách WiFi
    request->send(200, "text/html", 
        "<h3>Cấu hình đã được reset!</h3><p>Vui lòng cấu hình lại WiFi.</p><a href='/config'>Cấu hình WiFi</a>");
}

void WiFiManager::handleAPIStatus(AsyncWebServerRequest *request) {
    String json = "{\"status\":\"" + String(currentConfig.isValid ? "configured" : "not_configured") + 
                  "\",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
    request->send(200, "application/json", json);
}

bool WiFiManager::validateConfig(const String &ssid, const String &password, const String &topic) {
    return ssid.length() > 0 && password.length() > 0 && topic.length() > 0;
}
