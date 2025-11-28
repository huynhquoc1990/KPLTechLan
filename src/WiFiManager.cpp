// #include "WiFiManager.h"
// #include "Webservice.h"
// #include <ArduinoJson.h>
// // #include <esp_wifi.h>
// #include <esp_task_wdt.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>

// WiFiManager::WiFiManager(AsyncWebServer *webServer) 
//     : server(webServer), isLoggedIn(false) {
//     currentConfig.isValid = false;
// }

// WiFiManager::~WiFiManager() {
//     if (server) {
//         server->end();
//     }
// }

// bool WiFiManager::checkWiFiConfig() {
//     return loadConfig() && currentConfig.isValid;
// }

// bool WiFiManager::isRouterAvailable() {
//     if (!currentConfig.isValid) {
//         return false;
//     }
    
//     // Quick scan for router SSID (non-blocking, async scan)
//     // Note: We use a quick scan to check if router is broadcasting
//     WiFi.mode(WIFI_STA);
//     WiFi.disconnect();
//     esp_task_wdt_reset();
//     vTaskDelay(pdMS_TO_TICKS(100)); // Allow disconnect to complete
    
//     // Start async scan (non-blocking)
//     int scanResult = WiFi.scanNetworks(true, false); // true = async, false = don't show hidden
    
//     // Wait for scan to complete (max 5 seconds)
//     int attempts = 0;
//     while (WiFi.scanComplete() < 0 && attempts < 50) {
//         esp_task_wdt_reset();
//         vTaskDelay(pdMS_TO_TICKS(100));
//         attempts++;
//     }
    
//     int n = WiFi.scanComplete();
//     if (n < 0) {
//         // Scan didn't complete in time
//         WiFi.scanDelete();
//         return false;
//     }
    
//     // Check if our SSID is in the scan results
//     bool found = false;
//     for (int i = 0; i < n; i++) {
//         if (WiFi.SSID(i) == currentConfig.ssid) {
//             found = true;
//             Serial.printf("[Router Check] Found router SSID: %s (RSSI: %d dBm)\n", 
//                          currentConfig.ssid.c_str(), WiFi.RSSI(i));
//             break;
//         }
//     }
    
//     WiFi.scanDelete();
//     return found;
// }

// bool WiFiManager::connectToWiFi() {
//     if (!currentConfig.isValid) {
//         Serial.println("No valid WiFi configuration");
//         return false;
//     }
    
//     Serial.printf("Connecting to WiFi: %s\n", currentConfig.ssid.c_str());
    
//     // Prepare STA connection
//     WiFi.mode(WIFI_STA);
//     WiFi.setSleep(true);                 // save power while connecting
//     WiFi.setTxPower(WIFI_POWER_8_5dBm);  // moderate power
//     WiFi.begin(currentConfig.ssid.c_str(), currentConfig.password.c_str());
//     WiFi.setAutoConnect(true);
//     WiFi.setAutoReconnect(true);
//     WiFi.persistent(true);
    
//     // Increased timeout: 60 attempts × 500ms = 30 seconds
//     // This gives router more time to boot up after power loss
//     int attempts = 0;
//     const int maxAttempts = 60; // Increased from 30 to 60
    
//     while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
//         esp_task_wdt_reset();
//         vTaskDelay(pdMS_TO_TICKS(500));
//         if (attempts % 10 == 0) { // Print every 5 seconds
//             Serial.print(".");
//         }
//         attempts++;
//     }
    
//     if (WiFi.status() == WL_CONNECTED) {
//         // Keep modem sleep enabled to reduce temperature/power
//         WiFi.setSleep(true);
//         // Keep TX power low unless signal is weak
//         WiFi.setTxPower(WIFI_POWER_5dBm);
//         Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
//         return true;
//     } else {
//         Serial.println("\nWiFi connection failed");
//         return false;
//     }
// }

// void WiFiManager::startWiFiAP() {
//     Serial.println("[AP] Disconnecting from any existing WiFi network.");
//     WiFi.disconnect(true, true); // Xóa cấu hình cũ và ngắt kết nối
//     esp_task_wdt_reset();
//     vTaskDelay(pdMS_TO_TICKS(100));

//     Serial.println("[AP] Starting Access Point...");
//     // Prefer AP+STA to allow scanning without dropping AP
//     WiFi.mode(WIFI_AP_STA);
//     WiFi.setSleep(false); // keep AP stable
//     WiFi.setTxPower(WIFI_POWER_8_5dBm);
//     WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4); // channel 1, hidden=0, max 4 clients
//     // Note: Adjusting AP beacon interval is not available in Arduino API
//     Serial.printf("AP started: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
// }

// void WiFiManager::startConfigurationPortal() {
//     // Reduce CPU while in configuration to lower heat
//     setCpuFrequencyMhz(80);
//     // Inform system we're in config portal for thermal control
//     extern bool inConfigPortal;
//     inConfigPortal = true;
//     startWiFiAP();
//     scanWiFi(); // Quét một lần duy nhất ở đây
//     // Ensure AP is up after scan (redundant safety)
//     if (WiFi.getMode() != WIFI_MODE_AP) {
//         Serial.println("[AP] Safety check: AP not active, starting again...");
//         startWiFiAP();
//     }
//     setupWebServer();
//     Serial.println("Configuration portal is active.");
// }

// void WiFiManager::scanWiFi() {
//     Serial.println("\n[SCAN] Starting WiFi scan...");
    
//     Serial.println("[SCAN] Switching to AP+STA for scanning (keep AP alive).");
//     WiFi.mode(WIFI_AP_STA);
//     esp_task_wdt_reset();
//     vTaskDelay(pdMS_TO_TICKS(500)); // Allow time for mode change
    
//     Serial.println("[SCAN] Deleting previous scan results.");
//     WiFi.scanDelete();
//     esp_task_wdt_reset();
//     vTaskDelay(pdMS_TO_TICKS(100));
    
//     Serial.println("[SCAN] Starting synchronous network scan...");
//     int scanResult = WiFi.scanNetworks(false, true); // false = sync, true = show hidden
    
//     Serial.printf("[SCAN] WiFi.scanNetworks() returned: %d\n", scanResult);

//     if (scanResult < 0) {
//         Serial.printf("[SCAN] ERROR: Scan failed with error code: %d\n", scanResult);
//         wifiList = "[]";
//         // Restore AP mode
//         WiFi.mode(WIFI_AP);
//         return;
//     }
    
//     Serial.printf("[SCAN] Scan completed successfully. Found %d networks.\n", scanResult);
    
//     wifiList = "[";
//     for (int i = 0; i < scanResult; i++) {
//         if (i > 0) wifiList += ",";
//         wifiList += "\"" + WiFi.SSID(i) + "\"";
//         Serial.printf("  > Network %d: %s (%ddBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
//     }
//     wifiList += "]";
    
//     WiFi.scanDelete();
    
//     // Ensure AP is configured after scanning
//     Serial.println("[SCAN] Ensuring AP is active after scan...");
//     WiFi.mode(WIFI_AP_STA);
//     WiFi.setSleep(false);
//     WiFi.setTxPower(WIFI_POWER_8_5dBm);
//     WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
//     Serial.printf("[SCAN] AP re-started: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
//     esp_task_wdt_reset();
//     vTaskDelay(pdMS_TO_TICKS(100));
    
//     Serial.println("[SCAN] WiFi scan completed successfully.");
// }

// void WiFiManager::setupWebServer() {
//     if (!server) return;
    
//     setupRoutes();
//     server->begin();
//     Serial.println("Web server started");
// }

// void WiFiManager::startWebServer() {
//     if (server) {
//         server->begin();
//     }
// }

// void WiFiManager::stopWebServer() {
//     if (server) {
//         server->end();
//     }
// }

// bool WiFiManager::loadConfig() {
//     String configData = readFileConfig("/config.txt");
//     if (configData.isEmpty()) {
//         currentConfig.isValid = false;
//         return false;
//     }
    
//     int split1 = configData.indexOf('\n');
//     int split2 = configData.lastIndexOf('\n');
    
//     if (split1 == -1 || split2 == -1 || split1 == split2) {
//         currentConfig.isValid = false;
//         return false;
//     }
    
//     currentConfig.ssid = configData.substring(0, split1);
//     currentConfig.password = configData.substring(split1 + 1, split2);
//     currentConfig.topic = configData.substring(split2 + 1);
    
//     currentConfig.isValid = validateConfig(currentConfig.ssid, currentConfig.password, currentConfig.topic);
    
//     if (currentConfig.isValid) {
//         // Update global TopicMqtt with loaded ID
//         strncpy(TopicMqtt, currentConfig.topic.c_str(), sizeof(TopicMqtt) - 1);
//         TopicMqtt[sizeof(TopicMqtt) - 1] = '\0'; // Ensure null termination
        
//         Serial.printf("Loaded config: SSID=%s, Topic=%s\n", 
//                      currentConfig.ssid.c_str(), currentConfig.topic.c_str());
//     }
    
//     return currentConfig.isValid;
// }

// bool WiFiManager::saveConfig(const String &ssid, const String &password, const String &topic) {
//     // Use topic parameter from web form
//     if (!validateConfig(ssid, password, topic)) {
//         return false;
//     }
    
//     String configData = ssid + "\n" + password + "\n" + topic;
//     writeFileConfig("/config.txt", configData);
    
//     // Update current config
//     currentConfig.ssid = ssid;
//     currentConfig.password = password;
//     currentConfig.topic = topic;
//     currentConfig.isValid = true;
    
//     // Update global TopicMqtt with the new ID from form
//     strncpy(TopicMqtt, topic.c_str(), sizeof(TopicMqtt) - 1);
//     TopicMqtt[sizeof(TopicMqtt) - 1] = '\0'; // Ensure null termination
    
//     Serial.printf("WiFi configuration saved with Topic ID: %s\n", TopicMqtt);
//     return true;
// }

// bool WiFiManager::resetConfig() {
//     if (LittleFS.exists("/config.txt")) {
//         return LittleFS.remove("/config.txt");
//     }
//     return true;
// }

// void WiFiManager::setupRoutes() {
//     if (!server) return;
    
//     // Root route
//     server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         handleRoot(request);
//     });
    
//     // Login routes
//     server->on("/login", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", loginPage);
//     });
    
//     server->on("/login", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         handleLogin(request);
//     });
    
//     server->on("/logout", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         isLoggedIn = false;
//         request->redirect("/login");
//     });
    
//     // Config routes
//     server->on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         handleConfig(request);
//     });
    
//     server->on("/wifi_scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         handleWiFiScan(request);
//     });
    
//     server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         handleSaveConfig(request);
//     });
    
//     server->on("/reset_config", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         handleResetConfig(request);
//     });
    
//     // API routes
//     server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         handleAPIStatus(request);
//     });
// }

// void WiFiManager::handleRoot(AsyncWebServerRequest *request) {
//     if (!isLoggedIn) {
//         request->redirect("/login");
//     } else {
//         request->redirect("/config");
//     }
// }

// void WiFiManager::handleLogin(AsyncWebServerRequest *request) {
//     if (request->hasParam("username", true) && request->hasParam("password", true)) {
//         String username = request->getParam("username", true)->value();
//         String password = request->getParam("password", true)->value();
        
//         if (username == adminUser && password == adminPass) {
//             isLoggedIn = true;
//             request->redirect("/config");
//         } else {
//             request->send(200, "text/html", loginFail);
//         }
//     }
// }

// void WiFiManager::handleConfig(AsyncWebServerRequest *request) {
//     if (!isLoggedIn) {
//         request->redirect("/login");
//         return;
//     }
//     request->send(200, "text/html", configPage1);
// }

// void WiFiManager::handleWiFiScan(AsyncWebServerRequest *request) {
//     // Không cần quét lại, chỉ trả về danh sách đã lưu
//     request->send(200, "application/json", wifiList);
// }

// void WiFiManager::handleSaveConfig(AsyncWebServerRequest *request) {
//     if (!isLoggedIn) {
//         request->redirect("/login");
//         return;
//     }
    
//     if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
//         String ssid = request->getParam("ssid", true)->value();
//         String password = request->getParam("password", true)->value();
//         String topic = request->getParam("id", true)->value();
        
//         if (saveConfig(ssid, password, topic)) {
//             request->send(200, "text/html", result);
//             delay(2000); // Đợi để client nhận được response
//             ESP.restart(); // Khởi động lại ở đây
//         } else {
//             request->send(200, "text/html", serverFail);
//         }
//     }
// }

// void WiFiManager::handleResetConfig(AsyncWebServerRequest *request) {
//     if (!isLoggedIn) {
//         request->redirect("/login");
//         return;
//     }
    
//     resetConfig();
//     // Không cần scan lại vì AP mode đang hoạt động và đã có danh sách WiFi
//     request->send(200, "text/html", 
//         "<h3>Cấu hình đã được reset!</h3><p>Vui lòng cấu hình lại WiFi.</p><a href='/config'>Cấu hình WiFi</a>");
// }

// void WiFiManager::handleAPIStatus(AsyncWebServerRequest *request) {
//     String json = "{\"status\":\"" + String(currentConfig.isValid ? "configured" : "not_configured") + 
//                   "\",\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
//     request->send(200, "application/json", json);
// }

// bool WiFiManager::validateConfig(const String &ssid, const String &password, const String &topic) {
//     return ssid.length() > 0 && password.length() > 0 && topic.length() > 0;
// }


#include "WiFiManager.h"
#include "Webservice.h"
#include <ArduinoJson.h>
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

// ========== FIX 1: Viết lại hàm isRouterAvailable() ==========
bool WiFiManager::isRouterAvailable() {
    if (!currentConfig.isValid) {
        Serial.println("[Router Check] No valid config");
        return false;
    }
    
    Serial.println("[Router Check] Scanning for router...");
    esp_task_wdt_reset();
    
    // Đảm bảo ở chế độ STA để scan
    if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(500)); // Đợi mode switch ổn định
    }
    
    // Xóa scan results cũ
    WiFi.scanDelete();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Scan đồng bộ (sync) - đáng tin cậy hơn async
    int networksFound = WiFi.scanNetworks(false, true); // false=sync, true=show_hidden
    
    if (networksFound < 0) {
        Serial.println("[Router Check] Scan failed!");
        WiFi.scanDelete();
        return false;
    }
    
    Serial.printf("[Router Check] Found %d network(s)\n", networksFound);
    
    bool routerFound = false;
    for (int i = 0; i < networksFound; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        
        Serial.printf("  [%d] %-32s | %4d dBm\n", i, ssid.c_str(), rssi);
        
        if (ssid == currentConfig.ssid) {
            Serial.printf("[Router Check] ✓ Target router '%s' FOUND! (RSSI: %d dBm)\n", 
                         currentConfig.ssid.c_str(), rssi);
            routerFound = true;
            // Không break để in hết danh sách (debug)
        }
    }
    
    WiFi.scanDelete();
    
    if (!routerFound) {
        Serial.printf("[Router Check] ✗ Router '%s' NOT found\n", currentConfig.ssid.c_str());
    }
    
    return routerFound;
}

// ========== FIX 2: Viết lại hàm connectToWiFi() với retry logic ==========
bool WiFiManager::connectToWiFi() {
    if (!currentConfig.isValid) {
        Serial.println("[WiFi] No valid WiFi configuration");
        return false;
    }
    
    Serial.println("\n========== WiFi Connection Process ==========");
    Serial.printf("[WiFi] Target SSID: %s\n", currentConfig.ssid.c_str());
    
    const int MAX_ROUTER_CHECK_RETRIES = 20;  // Tối đa 20 lần check router
    int routerCheckAttempts = 0;
    
    // BƯỚC 1: Kiểm tra và chờ router available
    while (routerCheckAttempts < MAX_ROUTER_CHECK_RETRIES) {
        esp_task_wdt_reset();
        
        // Disconnect stale connection nếu có
        if (WiFi.status() != WL_IDLE_STATUS && WiFi.status() != WL_DISCONNECTED) {
            Serial.println("[WiFi] Disconnecting stale connection...");
            WiFi.disconnect(true);  // true = clear credentials
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // Reset WiFi stack mỗi 5 lần thử
        if (routerCheckAttempts > 0 && routerCheckAttempts % 5 == 0) {
            Serial.println("[WiFi] Resetting WiFi stack...");
            WiFi.mode(WIFI_OFF);
            vTaskDelay(pdMS_TO_TICKS(500));
            WiFi.mode(WIFI_STA);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // Kiểm tra router có available không
        Serial.printf("[WiFi] Checking router availability (Attempt %d/%d)...\n", 
                      routerCheckAttempts + 1, MAX_ROUTER_CHECK_RETRIES);
        
        bool routerAvailable = isRouterAvailable();
        
        if (routerAvailable) {
            Serial.println("[WiFi] ✓ Router is available!");
            break;
        }
        
        routerCheckAttempts++;
        
        if (routerCheckAttempts < MAX_ROUTER_CHECK_RETRIES) {
            // Chờ lâu hơn ở những lần đầu (router đang boot)
            int waitSeconds = (routerCheckAttempts <= 3) ? 10 : 5;
            Serial.printf("[WiFi] Router not found. Waiting %d seconds...\n", waitSeconds);
            
            for (int i = 0; i < waitSeconds; i++) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
    
    // Kiểm tra xem đã tìm thấy router chưa
    if (routerCheckAttempts >= MAX_ROUTER_CHECK_RETRIES) {
        Serial.println("[WiFi] ✗ Router not available after max retries!");
        Serial.println("[WiFi] Performing full WiFi reset...");
        
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        Serial.println("============================================\n");
        return false;
    }
    
    // BƯỚC 2: Router available, bắt đầu kết nối
    Serial.println("[WiFi] Starting connection...");
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(currentConfig.ssid.c_str(), currentConfig.password.c_str());
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Tăng timeout lên 60 giây (120 x 500ms)
    int connectAttempts = 0;
    const int maxConnectAttempts = 120;
    
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED && connectAttempts < maxConnectAttempts) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
        
        if (connectAttempts % 10 == 0) {
            Serial.print(".");
        }
        
        connectAttempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setSleep(true);
        WiFi.setTxPower(WIFI_POWER_5dBm);
        
        Serial.println("[WiFi] ✓ Connected successfully!");
        Serial.printf("[WiFi] IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WiFi] Signal: %d dBm\n", WiFi.RSSI());
        Serial.printf("[WiFi] Channel: %d\n", WiFi.channel());
        Serial.println("============================================\n");
        
        return true;
    } else {
        Serial.println("[WiFi] ✗ Connection failed!");
        Serial.println("============================================\n");
        return false;
    }
}

// ========== FIX 3: Cải thiện startWiFiAP() ==========
void WiFiManager::startWiFiAP() {
    Serial.println("[AP] Starting Access Point mode...");
    
    // Disconnect và clear credentials
    WiFi.disconnect(true, true);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500)); // Đợi lâu hơn
    
    // Chuyển sang AP+STA mode
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Start AP
    bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
    
    if (apStarted) {
        Serial.printf("[AP] ✓ Started successfully\n");
        Serial.printf("[AP] SSID: %s\n", AP_SSID);
        Serial.printf("[AP] IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[AP] ✗ Failed to start!");
    }
}

void WiFiManager::startConfigurationPortal() {
    // Giảm CPU để giảm nhiệt
    setCpuFrequencyMhz(80);
    
    extern bool inConfigPortal;
    inConfigPortal = true;
    
    startWiFiAP();
    scanWiFi();
    
    // Đảm bảo AP vẫn hoạt động sau scan
    if (WiFi.getMode() != WIFI_AP_STA && WiFi.getMode() != WIFI_AP) {
        Serial.println("[AP] Restarting AP after scan...");
        startWiFiAP();
    }
    
    setupWebServer();
    Serial.println("[Config Portal] Active and ready");
}

// ========== FIX 4: Cải thiện scanWiFi() ==========
void WiFiManager::scanWiFi() {
    Serial.println("\n[SCAN] Starting WiFi scan...");
    esp_task_wdt_reset();
    
    // Chuyển sang AP+STA mode
    Serial.println("[SCAN] Switching to AP+STA mode...");
    WiFi.mode(WIFI_AP_STA);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Tăng delay để ổn định
    
    // Xóa scan cũ
    WiFi.scanDelete();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Scan đồng bộ
    Serial.println("[SCAN] Scanning networks...");
    int scanResult = WiFi.scanNetworks(false, true); // sync, show hidden
    
    Serial.printf("[SCAN] Result: %d\n", scanResult);
    
    if (scanResult < 0) {
        Serial.printf("[SCAN] ✗ Scan failed (error: %d)\n", scanResult);
        wifiList = "[]";
        
        // Khôi phục AP mode
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
        return;
    }
    
    Serial.printf("[SCAN] ✓ Found %d network(s)\n", scanResult);
    
    // Build JSON list
    wifiList = "[";
    for (int i = 0; i < scanResult; i++) {
        if (i > 0) wifiList += ",";
        
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        
        wifiList += "\"" + ssid + "\"";
        Serial.printf("  [%d] %-32s | %4d dBm\n", i + 1, ssid.c_str(), rssi);
    }
    wifiList += "]";
    
    WiFi.scanDelete();
    
    // Đảm bảo AP vẫn hoạt động
    Serial.println("[SCAN] Ensuring AP is active...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 4);
    
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    Serial.printf("[SCAN] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[SCAN] Completed successfully\n");
}

void WiFiManager::setupWebServer() {
    if (!server) return;
    setupRoutes();
    server->begin();
    Serial.println("[Web Server] Started");
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
        strncpy(TopicMqtt, currentConfig.topic.c_str(), sizeof(TopicMqtt) - 1);
        TopicMqtt[sizeof(TopicMqtt) - 1] = '\0';
        
        Serial.printf("[Config] Loaded: SSID=%s, Topic=%s\n", 
                     currentConfig.ssid.c_str(), currentConfig.topic.c_str());
    }
    
    return currentConfig.isValid;
}

bool WiFiManager::saveConfig(const String &ssid, const String &password, const String &topic) {
    if (!validateConfig(ssid, password, topic)) {
        return false;
    }
    
    String configData = ssid + "\n" + password + "\n" + topic;
    writeFileConfig("/config.txt", configData);
    
    currentConfig.ssid = ssid;
    currentConfig.password = password;
    currentConfig.topic = topic;
    currentConfig.isValid = true;
    
    strncpy(TopicMqtt, topic.c_str(), sizeof(TopicMqtt) - 1);
    TopicMqtt[sizeof(TopicMqtt) - 1] = '\0';
    
    Serial.printf("[Config] Saved: Topic ID = %s\n", TopicMqtt);
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
    
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleRoot(request);
    });
    
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
            delay(2000);
            ESP.restart();
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
