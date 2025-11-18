#include "Settings.h"
#include <Arduino.h>

// Define variables
const char* configFile = "/config.txt";
const char* adminUser = ADMIN_USERNAME;
const char* adminPass = ADMIN_PASSWORD;

const char* mqttServer = "103.57.221.161";
const int   mqttPort = 1883;                  
const char* mqttUser = MQTT_USERNAME;
const char* mqttPassword = MQTT_PASSWORD;

const char* ntpServer = "time.google.com";
const long  gmtOffset_sec = 3600 * 7;
const int   daylightOffset_sec = 0;

char TopicMqtt[32] = "QA-T01-V01";
char wifi_ssid[32] = "Quoc Thu";
char wifi_password[64] = "T@nqu0c1";

const char* TopicLogError = "/Error/";
const char* TopicRestart  = "/Restart/";
const char* TopicGetLogIdLoss = "/GetLogIdLoss/";
const char* TopicSendData = "/GetData/";
const char* TopicStatus   = "/GetStatus/";
const char* TopicShift    = "/Shift/";
const char* TopicChange   = "/Change/";
const char* TopicOTA      = "/OTA/";
const char* TopicUpdatePrice = "/UpdatePrice";
const char* TopicGetPrice = "/GetPrice";
const char* TopicRequestLog = "/RequestLog";
// sửa thông tin phiên bản hardware vào đây
const char* hardwareVersion = "ASR-A2-1716"; // KC868-A2-1305 là phiên bản hardware của thiết bị

const uint8_t idVoiList[] = {99};
