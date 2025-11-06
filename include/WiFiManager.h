#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "Settings.h"

class WiFiManager {
private:
    AsyncWebServer *server;
    String wifiList;
    bool isLoggedIn;
    
    // WiFi configuration
    struct WiFiConfig {
        String ssid;
        String password;
        String topic;
        bool isValid;
    };
    
    WiFiConfig currentConfig;
    
public:
    WiFiManager(AsyncWebServer *webServer);
    ~WiFiManager();
    
    // WiFi management
    bool checkWiFiConfig();
    bool connectToWiFi();
    void startWiFiAP();
    void scanWiFi();
    bool isRouterAvailable(); // Check if configured router SSID is broadcasting
    
    // Web server management
    void setupWebServer();
    void startWebServer();
    void startConfigurationPortal();
    void stopWebServer();
    
    // Configuration management
    bool loadConfig();
    bool saveConfig(const String &ssid, const String &password, const String &topic);
    bool resetConfig();
    
    // Getters
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    String getLocalIP() const { return WiFi.localIP().toString(); }
    String getAPIP() const { return WiFi.softAPIP().toString(); }
    const WiFiConfig& getConfig() const { return currentConfig; }
    
private:
    // Web server routes
    void setupRoutes();
    void handleRoot(AsyncWebServerRequest *request);
    void handleLogin(AsyncWebServerRequest *request);
    void handleConfig(AsyncWebServerRequest *request);
    void handleWiFiScan(AsyncWebServerRequest *request);
    void handleSaveConfig(AsyncWebServerRequest *request);
    void handleResetConfig(AsyncWebServerRequest *request);
    void handleAPIStatus(AsyncWebServerRequest *request);
    
    // Helper functions
    bool validateConfig(const String &ssid, const String &password, const String &topic);
};

// Global instance defined in main.cpp
extern WiFiManager* wifiManager;

// Function declarations for C compatibility
// These are now handled by the WiFiManager class instance
// bool checkWiFiConfig();
// bool connectToWiFi();
// void startWiFiAP();
// void createWebServer();

#endif // WIFI_MANAGER_H
