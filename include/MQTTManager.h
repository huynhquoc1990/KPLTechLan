#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "structdata.h"
#include "Settings.h"

class MQTTManager {
private:
    PubSubClient *client;
    WiFiClient *wifiClient;
    
    // MQTT topics
    char fullTopic[64];
    char topicStatus[64];
    char topicError[64];
    char topicRestart[64];
    char topicGetLogIdLoss[64];
    char topicShift[64];
    char topicChange[64];
    
    // Connection state
    bool m_isConnected;
    unsigned long lastReconnectAttempt;
    int reconnectAttempts;
    
    // Data queue
    QueueHandle_t dataQueue;
    
public:
    MQTTManager(PubSubClient *mqttClient, WiFiClient *wifi);
    ~MQTTManager();
    
    // Connection management
    bool connect();
    bool reconnect();
    void disconnect();
    bool isConnected() const { return client->connected(); }
    
    // Topic management
    void setupTopics(const char* companyMst);
    void subscribeToTopics();
    
    // Data handling
    bool publishData(const PumpLog &log);
    bool publishStatus(const DeviceStatus &status);
    bool publishError(const char* error);
    
    // Queue management
    bool enqueueData(const PumpLog &log);
    bool dequeueData(PumpLog &log);
    int getQueueSize() const;
    
    // Callback handling
    void setCallback(void (*callback)(char*, byte*, unsigned int));
    void loop();
    
    // Configuration
    void setServer(const char* server, int port);
    void setCredentials(const char* username, const char* password);
    void setClientId(const char* clientId);
    
    // Getters
    const char* getFullTopic() const { return fullTopic; }
    const char* getTopicStatus() const { return topicStatus; }
    const char* getTopicError() const { return topicError; }
    
private:
    // Helper functions
    bool publishMessage(const char* topic, const char* message);
    bool publishJSON(const char* topic, const JsonDocument& doc);
    void handleReconnect();
};

// Global instance
extern MQTTManager* mqttManager;

// Function declarations for C compatibility
void setupMQTTTopics();
void connectMQTT();
void sendMQTTData(const PumpLog &log);

#endif // MQTT_MANAGER_H
