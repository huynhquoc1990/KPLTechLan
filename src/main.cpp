#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// Forward declarations for OTA functions
void performOTAUpdate(const char* firmwareURL);
void performOTAUpdateViaAPI(const String& apiEndpoint, const String& ftpUrl);

// Local includes
#include "Settings.h"
#include "structdata.h"
#include "FlashFile.h"
#include "Inits.h"
#include "Api.h"
#include "TTL.h"
#include "Setup.h"
#include "Webservice.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "RS485Manager.h"
#include "SystemManager.h"
#include "FlashFile.h"

// ============================================================================
// GLOBAL VARIABLES - Tối ưu hóa memory allocation
// ============================================================================

// Core system objects - sử dụng smart pointers nếu có thể
static DeviceStatus deviceStatus;
static CompanyInfo companyInfo;
static Settings settings;
static TimeSetup timeSetup;
static GetIdLogLoss receivedMessage;
bool inConfigPortal = false;

// System state
static uint32_t currentId = 0;
static bool statusConnected = false;
static bool isLoggedIn = false;
static bool mqttTopicsConfigured = false;
static unsigned long counterReset = 0;
static unsigned long lastHeapCheck = 0;
static String systemStatus = "OK";
static String lastError = "";

// MQTT topics - tối ưu memory allocation
static char fullTopic[64];
static char topicStatus[64];
static char topicError[64];           // full topic with device id (for publish if needed)
static char topicErrorSub[64];        // wildcard subscription (e.g., 11223311A/Error/#)
static char topicErrorPrefix[64];     // prefix match (e.g., 11223311A/Error/)
static char topicRestart[64];
static char topicGetLogIdLoss[64];
static char topicShift[64];
static char topicChange[64];
static char topicOTA[64];             // topic for OTA firmware update
static char topicChangePrice[64];     // topic for changing price

// FreeRTOS objects
static QueueHandle_t mqttQueue = NULL;
static QueueHandle_t logIdLossQueue = NULL;
static QueueHandle_t priceChangeQueue = NULL;  // Queue for price change requests
static SemaphoreHandle_t flashMutex = NULL;
static SemaphoreHandle_t systemMutex = NULL;

// Track last sent price change request for response publishing
static PriceChangeRequest lastPriceChangeRequest;

// Task handles
static TaskHandle_t rs485TaskHandle = NULL;
static TaskHandle_t mqttTaskHandle = NULL;
static TaskHandle_t wifiTaskHandle = NULL;
static TaskHandle_t webServerTaskHandle = NULL;
static TaskHandle_t resendLogRequestTaskHandle = NULL;

// WiFi objects
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static AsyncWebServer webServer(80);
WiFiManager * wifiManager = nullptr;

// Reset button state
static struct
{
  unsigned long pressTime = 0;
  bool isPressed = false;
  bool inProgress = false;
} resetButton;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

// Task functions
void rs485Task(void *parameter);
void mqttTask(void *parameter);
void wifiTask(void *parameter);
void webServerTask(void *parameter);
void wifiRescanTask(void *parameter);

// System functions
void systemInit();
void systemCheck();
void handleResetButton();
void checkHeap();
void setupTime();
void setSystemStatus(const String &status, const String &error = "");
void adjustThermals();

// MQTT functions
void mqttCallback(char *topic, byte *payload, unsigned int length);
void setupMQTTTopics();
void connectMQTT();
void sendDeviceStatus();

// Missing function declarations
void sendLogRequest(uint32_t logId);
void processAllVoi(TimeSetup *time);
uint8_t calculateChecksum_LogData(const uint8_t *data, size_t length);
void ganLog(byte *buffer, PumpLog &log);
void ConnectedKPLBox(void *param);
void resendLogRequest(void *param);
void blinkOutput2Connected();
void BlinkOutput2Task(void *param);
void readMacEsp();
// void performOTAUpdate(const char* firmwareURL);
void performOTAUpdateViaAPI(const String& apiEndpoint, const String& ftpUrl);
void sendPriceChangeCommand(const PriceChangeRequest &request);

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================

// dùng để kiểm tra có phát sinh giao dịch ko, nếu có reset biến này. và 60 phút nếu không có giao dịch thì restart esp
unsigned long checkLogSend = 0;


void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n===========================================");
  Serial.println("=== KPL Gas Device Starting ===");
  
  // Check reset reason
  esp_reset_reason_t reset_reason = esp_reset_reason();
  Serial.printf("Reset reason: %d ", reset_reason);
  switch(reset_reason)
  {
    case ESP_RST_UNKNOWN:   Serial.println("(UNKNOWN)"); break;
    case ESP_RST_POWERON:   Serial.println("(POWER ON)"); break;
    case ESP_RST_EXT:       Serial.println("(EXTERNAL PIN)"); break;
    case ESP_RST_SW:        Serial.println("(SOFTWARE)"); break;
    case ESP_RST_PANIC:     Serial.println("(PANIC/EXCEPTION)"); break;
    case ESP_RST_INT_WDT:   Serial.println("(⚠️ INTERRUPT WDT)"); break;
    case ESP_RST_TASK_WDT:  Serial.println("(⚠️ TASK WDT TIMEOUT)"); break;
    case ESP_RST_WDT:       Serial.println("(⚠️ OTHER WDT)"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("(DEEP SLEEP)"); break;
    case ESP_RST_BROWNOUT:  Serial.println("(BROWNOUT)"); break;
    case ESP_RST_SDIO:      Serial.println("(SDIO)"); break;
    default:                Serial.println("(OTHER)"); break;
  }
  Serial.println("===========================================\n");
  
  // Initialize system
  systemInit();
  // Create tasks with optimized stack sizes
  xTaskCreatePinnedToCore(rs485Task, "RS485", 8192, NULL, 3, &rs485TaskHandle, 0);
  xTaskCreatePinnedToCore(wifiTask, "WiFi", 8192, NULL, 2, &wifiTaskHandle, 1);
  xTaskCreatePinnedToCore(mqttTask, "MQTT", 8192, NULL, 2, &mqttTaskHandle, 1);
  // xTaskCreatePinnedToCore(resendLogRequest, "ResendLogRequest", 8192, NULL, 2, &resendLogRequestTaskHandle, 1);
  
  Serial.println("System initialized successfully");
}

void loop()
{
  // Main loop chỉ xử lý các tác vụ nhẹ
  handleResetButton();
  systemCheck();

  // Yield cho các task khác
  vTaskDelay(pdMS_TO_TICKS(100));
  yield();
  // Feed watchdog for loopTask explicitly
  esp_task_wdt_reset();
}

void readMacEsp()
{
  Serial.println("Reading Mac ESP...");
  // read địa chỉ mac của esp
  uint64_t chipid = ESP.getEfuseMac();
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          (uint8_t)(chipid >> 40), (uint8_t)(chipid >> 32), (uint8_t)(chipid >> 24),
          (uint8_t)(chipid >> 16), (uint8_t)(chipid >> 8), (uint8_t)chipid);
  Serial.println("Mac: " + String(macStr));

  // call api để check mac có trong hệ thống hay không, sử dụng Phương thức Poss

  String url = String(API_BASE_URL) + API_CHECK_MAC_ENDPOINT;
  HTTPClient http;
    http.begin(url);
  http.addHeader("Content-Type", "application/json");
  DynamicJsonDocument doc(1024);
  doc["taikhoan"] = String(macStr);
  doc["idchinhanh"] = String(companyInfo.Mst);
  String jsonData;
  serializeJson(doc, jsonData); 
  Serial.println("jsonData get Mac: " + jsonData);
  int httpCode = http.POST(jsonData);

  // cần kiểm tra nội dung trả về nếu 'OK' thì đọc giá trị lên
  String response = http.getString();
  Serial.println("response: " + response);
  // check response có chứa 'OK' không [{"IsValid":true}]
  DynamicJsonDocument doc2(1024);  
  DeserializationError error = deserializeJson(doc2, response);
  Serial.println("error: " + String(error.c_str()));

  if (error && httpCode != 200) {
    Serial.println("Parse error");
    http.end();
    return;
  }
  
  bool isValid = false;
  if (doc2.is<JsonArray>()) {
    JsonArray arr = doc2.as<JsonArray>();
    if (!arr.isNull() && arr.size() > 0 && arr[0].containsKey("IsValid")) {
      isValid = arr[0]["IsValid"].as<bool>();
    }
  } else if (doc2.is<JsonObject>()) {
    JsonObject obj = doc2.as<JsonObject>();
    if (obj.containsKey("IsValid")) {
      isValid = obj["IsValid"].as<bool>();
    }
  }

  http.end();

  if (isValid) {
    Serial.println("Mac is in system");
  } else {
    Serial.println("Mac is not in system");
    while (true)
    {
      Serial.println("Mac is not in system");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
  // http.end();
}

// ============================================================================
// SYSTEM INITIALIZATION
// ============================================================================

void systemInit()
{
  // GPIO setup
  pinMode(OUT1, OUTPUT);
  pinMode(OUT2, OUTPUT);
  pinMode(RESET_CONFIG_PIN, INPUT_PULLUP);

  // Watchdog setup - 30s timeout with panic on timeout
  esp_task_wdt_init(30, true); // 30s timeout, true = panic and reset on timeout
  esp_task_wdt_add(NULL); // Add setup/loop task to WDT

  // Initialize RS485
  Serial2.begin(RS485BaudRate, SERIAL_8N1, RX_PIN, TX_PIN);

  // Initialize FreeRTOS objects
  mqttQueue = xQueueCreate(10, sizeof(PumpLog));
  logIdLossQueue = xQueueCreate(50, sizeof(DtaLogLoss));
  priceChangeQueue = xQueueCreate(10, sizeof(PriceChangeRequest));  // Queue for price changes
  flashMutex = xSemaphoreCreateMutex();
  systemMutex = xSemaphoreCreateMutex();

  if (!mqttQueue || !logIdLossQueue || !priceChangeQueue || !flashMutex || !systemMutex)
  {
    Serial.println("ERROR: Failed to create FreeRTOS objects!");
    setSystemStatus("ERROR", "Failed to create FreeRTOS objects");
    ESP.restart();
  }

  // Initialize file system
  if (!initLittleFS())
  {
    Serial.println("ERROR: Failed to initialize LittleFS!");
    setSystemStatus("ERROR", "Failed to initialize LittleFS");
    ESP.restart();
  }

  // Initialize WiFiManager
  wifiManager = new WiFiManager(&webServer);

  // Load system data
  readFlashSettings(flashMutex, deviceStatus, counterReset);
  currentId = initializeCurrentId(flashMutex);

  // Initialize WiFi
  WiFi.mode(WIFI_STA);

  // Initialize MQTT
  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setBufferSize(1024); // Increase buffer size from default 128
  mqttClient.setCallback(mqttCallback);
  
  Serial.printf("MQTT client initialized - Buffer size: %d\n", mqttClient.getBufferSize());

  // Send startup commands
  sendStartupCommand();
  delay(1000);
  sendStartupCommand();

  Serial.printf("System initialized - Current ID: %u\n", currentId);
}

// ============================================================================
// SYSTEM MONITORING
// ============================================================================

void systemCheck()
{
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  // Check every 10 seconds
  if (now - lastCheck >= 10000)
  {
    lastCheck = now;

    // Reset watchdog
    esp_task_wdt_reset();

    // Check heap memory
    checkHeap();

    checkLogSend++;
    if (checkLogSend >= 360)
    {
      ESP.restart();
      Serial.println("Check log send: " + String(checkLogSend));
    }

    // Adjust thermals based on die temperature
    adjustThermals();

    // Send status via MQTT if connected and topics configured
    if (mqttClient.connected() && mqttTopicsConfigured)
    {
      sendDeviceStatus();
    }

    // Check task states
    if (rs485TaskHandle && eTaskGetState(rs485TaskHandle) == eDeleted)
    {
      Serial.println("WARNING: RS485 task deleted, restarting...");
      setSystemStatus("WARNING", "RS485 task deleted and restarted");
      xTaskCreatePinnedToCore(rs485Task, "RS485", 8192, NULL, 3, &rs485TaskHandle, 0);
    }
  }
}

void checkHeap()
{
  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t minFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

  deviceStatus.heap = freeHeap;
  deviceStatus.free = minFreeHeap;
  deviceStatus.temperature = temperatureRead();
  // Optional: smooth die temperature to avoid spikes
  static float smoothedTemp = 0.0f;
  if (smoothedTemp == 0.0f) smoothedTemp = deviceStatus.temperature;
  smoothedTemp = 0.7f * smoothedTemp + 0.3f * deviceStatus.temperature;
  deviceStatus.temperature = smoothedTemp;
  deviceStatus.counterReset = counterReset;

  // Log memory status
  Serial.printf("Heap: %u free, %u min free, Temp: %.1f°C\n",
                freeHeap, minFreeHeap, deviceStatus.temperature);

  // Memory warning
  if (freeHeap < 10000)
  {
    Serial.println("WARNING: Low memory!");
    setSystemStatus("WARNING", "Low memory: " + String(freeHeap) + " bytes");
  }
  else if (systemStatus == "WARNING" && lastError.indexOf("Low memory") >= 0)
  {
    // Clear memory warning if heap is back to normal
    setSystemStatus("OK");
  }
}

void adjustThermals()
{
  static bool thermallyReduced = false;
  float dieTemp = deviceStatus.temperature;

  // In configuration portal, force minimal performance to keep temps low
  if (inConfigPortal)
  {
    setCpuFrequencyMhz(80);
    // Keep AP stable: disable modem sleep in AP mode
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    if (!thermallyReduced)
    {
      Serial.println("Thermal: CONFIG portal - CPU 80MHz, TX power 8.5dBm, modem sleep OFF");
      thermallyReduced = true;
    }
    return;
  }

  if (!thermallyReduced && dieTemp > 80.0f)
  {
    setCpuFrequencyMhz(160);
    WiFi.setSleep(true);
    WiFi.setTxPower(WIFI_POWER_2dBm);
    thermallyReduced = true;
    Serial.println("Thermal: reduced CPU to 160MHz, TX power 2dBm, modem sleep ON");
  }
  else if (thermallyReduced && dieTemp < 70.0f)
  {
    inConfigPortal = false;
    setCpuFrequencyMhz(240);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    thermallyReduced = false;
    Serial.println("Thermal: restored CPU to 240MHz, TX power 8.5dBm");
  }
}

void setSystemStatus(const String &status, const String &error)
{
  systemStatus = status;
  lastError = error;

  if (status != "OK")
  {
    Serial.printf("[STATUS] %s: %s\n", status.c_str(), error.c_str());
    // send status to mqtt
    if (mqttClient.connected())
    {
      sendDeviceStatus();
    }
    // mqttClient.loop();
  }
}

// ============================================================================
// RESET BUTTON HANDLING
// ============================================================================

void handleResetButton()
{
  int buttonState = digitalRead(RESET_CONFIG_PIN);

  if (buttonState == LOW)
  { // Button pressed
    if (!resetButton.isPressed)
    {
      resetButton.isPressed = true;
      resetButton.pressTime = millis();
      Serial.println("Reset button pressed - hold for 5 seconds");
      digitalWrite(OUT2, HIGH);
    }

    // Show countdown
    unsigned long pressDuration = millis() - resetButton.pressTime;
    if (pressDuration >= 5000 && !resetButton.inProgress)
    {
      resetButton.inProgress = true;
      Serial.println("Resetting WiFi configuration...");

      // Visual feedback
      for (int i = 0; i < 5; i++)
      {
        digitalWrite(OUT2, HIGH);
        delay(200);
        digitalWrite(OUT2, LOW);
        delay(200);
      }

      // Reset config
      if (LittleFS.exists("/config.txt"))
      {
        LittleFS.remove("/config.txt");
        Serial.println("WiFi config deleted");
      }

      Serial.println("Restarting in 3 seconds...");
      delay(3000);
      ESP.restart();
    }
  }
  else
  {
    if (resetButton.isPressed)
    {
      unsigned long pressDuration = millis() - resetButton.pressTime;
      resetButton.isPressed = false;
      resetButton.inProgress = false;
      digitalWrite(OUT2, LOW);
      Serial.println("Reset button released");

      // Short press: trigger WiFi rescan if in config portal (AP mode)
      if (pressDuration < 1000 && inConfigPortal && wifiManager != nullptr)
      {
        Serial.println("Short press detected - triggering WiFi rescan...");
        xTaskCreate(wifiRescanTask, "WiFiRescan", 4096, NULL, 2, NULL);
      }
    }
  }
}

// ============================================================================
// WIFI TASK
// ============================================================================

void wifiTask(void *parameter)
{
  Serial.println("WiFi task started");
  esp_task_wdt_add(NULL);

  // Chỉ kiểm tra config một lần khi khởi động
  if (!wifiManager->checkWiFiConfig())
  {
    Serial.println("No valid WiFi config found. Entering AP configuration mode.");

    // Tạm dừng các task khác để ưu tiên
    if (rs485TaskHandle != NULL)
      vTaskSuspend(rs485TaskHandle);
    if (mqttTaskHandle != NULL)
      vTaskSuspend(mqttTaskHandle);

    // Bật chế độ AP, quét Wi-Fi một lần và khởi động web server
    wifiManager->startConfigurationPortal();

    Serial.println("AP Mode and Web Server started. WiFi task will now suspend.");

    // Restore CPU when leaving config portal (will resume after restart)
    setCpuFrequencyMhz(240);
    // Deregister from WDT before suspending indefinitely
    esp_task_wdt_delete(NULL);
    // Tự treo task này, chờ restart từ web server
    vTaskSuspend(NULL);
  }

  // Nếu có config, tiếp tục vòng lặp kết nối bình thường
  while (true)
  {
    esp_task_wdt_reset();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Attempting to connect to saved WiFi...");
      if (wifiManager->connectToWiFi())
      {
        Serial.println("WiFi connected successfully.");
        setupTime();
        setupMQTTTopics();
        readMacEsp();
        statusConnected = true;
      }
      else
      {
        static uint32_t cooldownSeconds = 10; // exponential backoff, capped
        Serial.printf("WiFi connection failed, cooling radio for %lus...\n", cooldownSeconds);
        setSystemStatus("ERROR", "WiFi connection failed");

        // Turn radio off during cooldown to reduce temperature
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        setCpuFrequencyMhz(160);
        for (uint32_t i = 0; i < cooldownSeconds; i++) {
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Restore for next attempt
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(true);
        setCpuFrequencyMhz(240);

        // Increase backoff up to 60s
        if (cooldownSeconds < 60) {
          cooldownSeconds = cooldownSeconds < 30 ? cooldownSeconds * 2 : 60;
        }
      }
    }
    else
    {
      // Đã kết nối, kiểm tra định kỳ trong 30 step, mỗi step reset WDT
      for (int i = 0; i < 30; i++) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
    yield();
  }
}

// ============================================================================
// MQTT TASK
// ============================================================================

void mqttTask(void *parameter)
{
  Serial.println("MQTT task started");
  esp_task_wdt_add(NULL);

  while (true)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      if (!mqttClient.connected())
      {
        connectMQTT();
      }
      else
      {
        // Process MQTT messages - CRITICAL for callback to work
        mqttClient.loop();

        // Process MQTT queue
        PumpLog log;
        if (xQueueReceive(mqttQueue, &log, pdMS_TO_TICKS(100)) == pdTRUE)
        {
          sendMQTTData(log);
        }
        
        // Additional loop call to ensure callback processing
        mqttClient.loop();
      }
    }

    // System monitoring (every 10 seconds)
    systemCheck();

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_task_wdt_reset();
    yield();
  }
}

// ============================================================================
// RS485 TASK
// ============================================================================

void rs485Task(void *parameter)
{
  Serial.println("RS485 task started");

  // Wait for system to stabilize before processing RS485 data
  Serial.println("[RS485] Waiting 3 seconds for system stabilization...");
  vTaskDelay(pdMS_TO_TICKS(3000));
  
  // Clear any buffered data from TTL boot sequence
  Serial.println("[RS485] Clearing boot buffer...");
  while (Serial2.available())
  {
    Serial2.read();
  }
  Serial.println("[RS485] Buffer cleared, ready to process data");

  byte buffer[LOG_SIZE];
  unsigned long lastSendTime = 0;
  unsigned long lastPriceChangeTime = 0;
  esp_task_wdt_add(NULL); // Add this task to WDT monitoring
  
  while (true)
  {
    // Feed watchdog at start of each cycle
    esp_task_wdt_reset();
    
    // Read RS485 data with error protection
    readRS485Data(buffer);

    // Priority 1: Process price change queue (one request per cycle, every 300ms)
    if (millis() - lastPriceChangeTime >= 300)
    {
      lastPriceChangeTime = millis();
      PriceChangeRequest priceRequest;
      if (xQueueReceive(priceChangeQueue, &priceRequest, 0) == pdTRUE)
      {
        Serial.printf("[RS485] Processing price change request for DeviceID=%d (Queue size: %d)\n", 
                      priceRequest.deviceId, uxQueueMessagesWaiting(priceChangeQueue));
        sendPriceChangeCommand(priceRequest);
      }
    }

    // Priority 2: Send log requests every 500ms (reduce heat/power)
    if (millis() - lastSendTime >= 500)
    {
      lastSendTime = millis();
      // Process log loss queue
      DtaLogLoss dataLog;
      if (xQueueReceive(logIdLossQueue, &dataLog, 0) == pdTRUE)
      {
        checkLogSend = 0; // dùng để kiểm tra có phát sinh giao dịch ko, nếu có reset biến này. 
        sendLogRequest(static_cast<uint32_t>(dataLog.Logid));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    yield();
  }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void setupTime()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  int retry = 0;
  const int maxRetries = 5;

  while (!getLocalTime(&timeinfo) && retry < maxRetries)
  {
    Serial.println("Failed to obtain time, retrying...");
    delay(2000);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    retry++;
  }

  if (retry < maxRetries)
  {
    setUpTime(&timeSetup, timeinfo);
    processAllVoi(&timeSetup);
    Serial.println("Time synchronized successfully");
    setSystemStatus("OK", ""); // Clear any previous time sync errors
  }
  else
  {
    Serial.println("Failed to sync time");
    setSystemStatus("WARNING", "Failed to synchronize time after " + String(maxRetries) + " attempts");
  }
}

void setupMQTTTopics()
{
  // Get company info
  callAPIGetSettingsMqtt(&settings, flashMutex);
  xTaskCreate(callAPIServerGetCompanyInfo, "getCompanyInfo", 2048, &companyInfo, 2, NULL);

  // Wait for company info
  vTaskDelay(pdMS_TO_TICKS(4000));

  // Build topics
  snprintf(fullTopic, sizeof(fullTopic), "%s%s%s", companyInfo.Mst, TopicSendData, TopicMqtt);
  snprintf(topicStatus, sizeof(topicStatus), "%s%s%s", companyInfo.Mst, TopicStatus, TopicMqtt);
  snprintf(topicError, sizeof(topicError), "%s%s%s", companyInfo.Mst, TopicLogError, TopicMqtt);
  // wildcard subscribe to all device ids under Error channel
  snprintf(topicErrorSub, sizeof(topicErrorSub), "%s%s#", companyInfo.Mst, TopicLogError);
  // prefix for quick check in callback
  snprintf(topicErrorPrefix, sizeof(topicErrorPrefix), "%s%s", companyInfo.Mst, TopicLogError);
  snprintf(topicRestart, sizeof(topicRestart), "%s%s%s", companyInfo.Mst, TopicRestart, TopicMqtt);
  snprintf(topicGetLogIdLoss, sizeof(topicGetLogIdLoss), "%s%s", companyInfo.Mst, TopicGetLogIdLoss);
  snprintf(topicShift, sizeof(topicShift), "%s%s%s", companyInfo.Mst, TopicShift, TopicMqtt);
  snprintf(topicChange, sizeof(topicChange), "%s%s%s", companyInfo.Mst, TopicChange, TopicMqtt);
  snprintf(topicOTA, sizeof(topicOTA), "%s/OTA/%s", companyInfo.Mst, TopicMqtt);
  snprintf(topicChangePrice, sizeof(topicChangePrice), "%s%s%s", companyInfo.Mst, TopicChangePrice, TopicMqtt);

  Serial.printf("MQTT topics configured - Company ID: %s (MST: %s)\n", companyInfo.Mst, companyInfo.Mst);
  mqttTopicsConfigured = true;

  // Subscribe to topics if MQTT is connected
  if (mqttClient.connected())
  {
    Serial.println("=== SUBSCRIBING TO MQTT TOPICS ===");
    
    bool sub1 = mqttClient.subscribe(topicErrorSub);
    bool sub2 = mqttClient.subscribe(topicRestart);
    bool sub3 = mqttClient.subscribe(topicGetLogIdLoss);
    bool sub4 = mqttClient.subscribe(topicChange);
    bool sub5 = mqttClient.subscribe(topicShift);
    bool sub6 = mqttClient.subscribe(topicOTA);
    bool sub7 = mqttClient.subscribe(topicChangePrice);

    Serial.printf("Subscription results:\n");
    Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
    Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
    Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
    Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
    Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
    Serial.printf("  OTA (%s): %s\n", topicOTA, sub6 ? "SUCCESS" : "FAILED");
    Serial.printf("  ChangePrice (%s): %s\n", topicChangePrice, sub7 ? "SUCCESS" : "FAILED");
    Serial.println("=== SUBSCRIPTION COMPLETE ===");
      }
      else
      {
    Serial.println("MQTT not connected, will subscribe when connected");
  }
}

void connectMQTT()
{
  Serial.println("Connecting to MQTT...");

  if (mqttClient.connect(TopicMqtt, mqttUser, mqttPassword))
  {
    Serial.println("MQTT connected");
    statusConnected = true;
    setSystemStatus("OK", ""); // Clear any MQTT connection errors

    // Subscribe to topics if they have been configured
    if (mqttTopicsConfigured)
    {
      Serial.println("=== RE-SUBSCRIBING TO MQTT TOPICS AFTER RECONNECT ===");
      
      bool sub1 = mqttClient.subscribe(topicErrorSub);
      bool sub2 = mqttClient.subscribe(topicRestart);
      bool sub3 = mqttClient.subscribe(topicGetLogIdLoss);
      bool sub4 = mqttClient.subscribe(topicChange);
      bool sub5 = mqttClient.subscribe(topicShift);
      bool sub6 = mqttClient.subscribe(topicOTA);
      bool sub7 = mqttClient.subscribe(topicChangePrice);

      Serial.printf("Re-subscription results:\n");
      Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
      Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
      Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
      Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
      Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
      Serial.printf("  OTA (%s): %s\n", topicOTA, sub6 ? "SUCCESS" : "FAILED");
      Serial.printf("  ChangePrice (%s): %s\n", topicChangePrice, sub7 ? "SUCCESS" : "FAILED");
      Serial.println("=== RE-SUBSCRIPTION COMPLETE ===");
    }
        }
        else
        {
    Serial.printf("MQTT connection failed. State: %d\n", mqttClient.state());
    setSystemStatus("ERROR", "MQTT connection failed - state: " + String(mqttClient.state()));
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void sendDeviceStatus()
{
  // Create JSON status data
  DynamicJsonDocument doc(1024);

  doc["idDevice"] = TopicMqtt;
  doc["companyId"] = companyInfo.Mst;
  doc["heap"] = deviceStatus.heap;
  doc["minFreeHeap"] = deviceStatus.free;
  doc["temperature"] = deviceStatus.temperature;
  doc["counterReset"] = deviceStatus.counterReset;
  doc["ipAddress"] = WiFi.localIP().toString();

  uint64_t chipid = ESP.getEfuseMac();
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          (uint8_t)(chipid >> 40), (uint8_t)(chipid >> 32), (uint8_t)(chipid >> 24),
          (uint8_t)(chipid >> 16), (uint8_t)(chipid >> 8), (uint8_t)chipid);
  // Serial.println("Mac: " + String(macStr));
  doc["macAddress"] = String(macStr);

  String jsonString;
  serializeJson(doc, jsonString);

  // Publish to status topic
  if (mqttClient.publish(topicStatus, jsonString.c_str()))
  {
    Serial.printf("Status sent: %s\n", jsonString.c_str());
    // Blink OUT2 to indicate internet connectivity (only if connected)
    if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
      blinkOutput2Connected();
    }
  }
  else
  {
    Serial.println("Failed to send device status");
    setSystemStatus("ERROR", "MQTT publish failed");
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.printf("=== MQTT CALLBACK TRIGGERED ===\n");
  Serial.printf("Topic: %s\n", topic);
  
  // Handle Restart command
  if (strcmp(topic, topicRestart) == 0)
  {
    Serial.println("Restart command received - restarting ESP32...");
    ESP.restart();
  }
  
  // Handle OTA Update command
  if (strcmp(topic, topicOTA) == 0)
  {
    Serial.println("OTA command received - parsing payload...");
    
    // Parse JSON payload
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error)
    {
      Serial.printf("OTA: JSON parse error: %s\n", error.c_str());
      setSystemStatus("ERROR", "OTA: Invalid JSON payload");
      return;
    }
    
    // Check if this update is for this specific device
    if (doc.containsKey("device"))
    {
      const char* targetDevice = doc["device"];
      if (strcmp(targetDevice, TopicMqtt) != 0)
      {
        Serial.printf("OTA: Update not for this device (target: %s, this: %s)\n", targetDevice, TopicMqtt);
        return;
      }
    }
    
    // Method 2: API endpoint + FTP URL (current)
    if (doc.containsKey("api") && doc.containsKey("ftpUrl"))
    {
      String apiEndpoint = doc["api"].as<String>();
      String ftpUrl = doc["ftpUrl"].as<String>();
      Serial.println("[MQTT] Received OTA command via API");
      performOTAUpdateViaAPI(apiEndpoint, ftpUrl);
      return;
    }

    // Method 1: Direct URL (for GitHub)
    if (doc.containsKey("url"))
    {
      const char* firmwareURL = doc["url"];
      Serial.println("[MQTT] Received OTA command via Direct URL");
      performOTAUpdate(firmwareURL);
      return;
    }
    
    // No valid OTA method found
    Serial.println("[MQTT ERROR] OTA payload is invalid. Expected 'url' or 'api'/'ftpUrl'.");
    setSystemStatus("ERROR", "OTA: Invalid payload");
  }

  // Match any 11223311A/Error/{anything} by prefix
  if (strncmp(topic, topicErrorPrefix, strlen(topicErrorPrefix)) == 0)
  {
    Serial.println("Error topic received - parsing payload...");
    parsePayload_IdLogLoss(payload, length, &receivedMessage, &companyInfo);
    Serial.printf("Parsed Idvoi: %s, Expected: %s\n", receivedMessage.Idvoi, TopicMqtt);

    if (strcmp(receivedMessage.Idvoi, TopicMqtt) == 0)
    {
      Serial.println("Idvoi matches - processing log loss...");
        if (uxQueueMessagesWaiting(logIdLossQueue) == 0)
        {
          TaskParams *taskParams = new TaskParams;
        taskParams->msg = new GetIdLogLoss(receivedMessage);
          taskParams->logIdLossQueue = logIdLossQueue;

        // Check heap before creating task
        size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        Serial.printf("Heap before creating API task: %u bytes\n", freeHeap);
        
        if (freeHeap < 20000) {
          Serial.println("Not enough heap memory for API task");
          delete taskParams->msg;
          delete taskParams;
          return;
        }
        
        // Reduce stack size from 40960 to 16384
        if (xTaskCreate(callAPIServerGetLogLoss, "GetData", 16384, taskParams, 3, NULL) != pdPASS)
        {
          Serial.println("Failed to create log loss task");
            delete taskParams->msg;
            delete taskParams;
          }
        else
        {
          Serial.println("Log loss task created successfully");
        }
    }
    else
    {
        Serial.println("Log loss queue is not empty, skipping...");
      }
    }
    else
    {
      Serial.println("Idvoi does not match, ignoring message");
    }
  }
  
  // Handle ChangePrice command
  if (strcmp(topic, topicChangePrice) == 0)
  {
    Serial.printf("[MQTT] ChangePrice command received - parsing payload...\n");
    Serial.printf("[MQTT] Current device MST: %s\n", companyInfo.Mst);
    Serial.printf("[MQTT] Payload length: %d bytes\n", length);
    
    // Print raw payload for debugging
    Serial.print("[MQTT] Raw payload: ");
    for (size_t i = 0; i < length && i < 500; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    
    // Parse JSON array payload
    DynamicJsonDocument doc(2048); // Increase size for array
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error)
    {
      Serial.printf("[MQTT] ChangePrice: JSON parse error: %s\n", error.c_str());
      setSystemStatus("ERROR", "ChangePrice: Invalid JSON payload");
      return;
    }
    
    // Check if payload is an array
    if (!doc.is<JsonArray>())
    {
      Serial.println("[MQTT] ChangePrice: Payload is not a JSON array");
      setSystemStatus("ERROR", "ChangePrice: Expected JSON array");
      return;
    }
    
    JsonArray priceArray = doc.as<JsonArray>();
    Serial.printf("[MQTT] ChangePrice: Received %d price entries\n", priceArray.size());
    
    // Parse and queue each price change request for RS485 task to process
    int queued = 0;
    int skipped = 0;
    uint8_t deviceIdSequence = 11; // Start from ID 11, increment for each valid entry
    
    for (JsonObject entry : priceArray)
    {
      // Get item object
      JsonObject item = entry["item"];
      if (item.isNull())
      {
        Serial.println("[MQTT] Missing 'item' field, skipping...");
        skipped++;
        continue;
      }
      
      const char* idChiNhanh = item["IDChiNhanh"] | "";
      const char* idDevice = item["IdDevice"] | "";
      
      // Check if this is for current company (compare IDChiNhanh with MST)
      if (strlen(idChiNhanh) > 0 && strcmp(idChiNhanh, companyInfo.Mst) != 0)
      {
        Serial.printf("[MQTT] Skipping - IDChiNhanh=%s doesn't match MST=%s\n", idChiNhanh, companyInfo.Mst);
        skipped++;
        continue;
      }
      
      Serial.printf("[MQTT] Processing Entry #%d: IDChiNhanh=%s, IdDevice=%s\n", 
                    deviceIdSequence - 10, idChiNhanh, idDevice);
      
      // Handle null UnitPrice
      if (item["UnitPrice"].isNull())
      {
        Serial.println("[MQTT] UnitPrice is null, skipping...");
        skipped++;
        continue;
      }
      
      float unitPrice = item["UnitPrice"] | 0.0f;
      Serial.printf("[MQTT] UnitPrice=%.2f\n", unitPrice);
      
      // Assign sequential device ID (11, 12, 13, ... up to 20 max)
      uint8_t deviceIdNum = deviceIdSequence;
      
      if (deviceIdNum > 20)
      {
        Serial.printf("[MQTT] Maximum devices reached (max 10, ID 11-20)\n");
        skipped++;
        continue;
      }
      
      Serial.printf("[MQTT] Assigned RS485 Device ID: %d for IdDevice=%s\n", deviceIdNum, idDevice);
      
      // Create price change request
      PriceChangeRequest request;
      request.deviceId = deviceIdNum;
      request.unitPrice = unitPrice;
      strncpy(request.idDevice, idDevice, sizeof(request.idDevice) - 1);
      request.idDevice[sizeof(request.idDevice) - 1] = '\0'; // Ensure null termination
      strncpy(request.idChiNhanh, idChiNhanh, sizeof(request.idChiNhanh) - 1);
      request.idChiNhanh[sizeof(request.idChiNhanh) - 1] = '\0'; // Ensure null termination
      
      // Queue the request for RS485 task to process
      if (xQueueSend(priceChangeQueue, &request, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        queued++;
        Serial.printf("[MQTT] ✓ Queued price change: DeviceID=%d, Price=%.2f\n", deviceIdNum, unitPrice);
        deviceIdSequence++; // Increment for next entry
      }
      else
      {
        skipped++;
        Serial.printf("[MQTT] ✗ Failed to queue: Queue full for DeviceID=%d\n", deviceIdNum);
      }
    }
    
    // Send summary
    Serial.printf("\n[MQTT] ChangePrice Summary: Queued=%d, Skipped=%d, Total=%d\n", 
                  queued, skipped, priceArray.size());
    Serial.printf("[MQTT] RS485 task will process %d price change(s)\n", queued);
    
    if (queued > 0) {
      setSystemStatus("OK", String("Queued ") + String(queued) + " price change(s)");
    } else {
      setSystemStatus("WARNING", "No price changes queued");
    }
  }
  
  Serial.println("=== MQTT CALLBACK FINISHED ===\n");
}

void sendMQTTData(const PumpLog &log)
{
  String jsonData = convertPumpLogToJson(log);

  int retryCount = 0;
  const int maxRetries = 3;
  esp_task_wdt_reset();

  while (retryCount < maxRetries)
  {
    if (mqttClient.publish(fullTopic, jsonData.c_str()))
    {
      Serial.println("MQTT data sent successfully");
      break;
    }
    else
    {
      Serial.printf("MQTT send failed (Attempt %d/%d)\n", retryCount + 1, maxRetries);
      retryCount++;
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    esp_task_wdt_reset();
    yield();
  }

  if (retryCount == maxRetries)
  {
    Serial.println("ERROR: MQTT send failed after maximum retries");
    setSystemStatus("ERROR", "MQTT send failed after " + String(maxRetries) + " retries");
  }
}

void readRS485Data(byte *buffer)
{
  if (!Serial2.available())
  {
    return;
  }
  
  // Limit processing to prevent overflow from TTL boot spam
  static unsigned long lastReadTime = 0;
  static int consecutiveReads = 0;
  unsigned long now = millis();
  
  if (now - lastReadTime < 50)
  {
    consecutiveReads++;
    if (consecutiveReads > 20)
    {
      Serial.println("[RS485 READ] ⚠️ Too many consecutive reads, clearing buffer to prevent overflow");
      while (Serial2.available())
      {
        Serial2.read();
      }
      consecutiveReads = 0;
      return;
    }
  }
  else
  {
    consecutiveReads = 0;
  }
  lastReadTime = now;

  // Peek first byte to determine message type
  int firstByte = Serial2.peek();
  
  // Case 0: Echo of sent command - Format: [9][ID][...] (10 bytes) - DISCARD IT
  if (firstByte == 9 && Serial2.available() >= 10)
  {
    uint8_t echoBuffer[10];
    Serial2.readBytes(echoBuffer, 10);
    Serial.printf("[RS485 READ] Discarding echo: [0x%02X][0x%02X]...\n", echoBuffer[0], echoBuffer[1]);
    return;
  }
  
  // Case 1: Price Change Response - Format: [7][ID][S/E][8] (4 bytes)
  if (firstByte == 7 && Serial2.available() >= 4)
  {
    uint8_t priceResponse[4];
    if (Serial2.readBytes(priceResponse, 4) == 4)
    {
      // Validate format: [7][ID][Status][8]
      if (priceResponse[0] == 7 && priceResponse[3] == 8)
      {
        uint8_t deviceId = priceResponse[1];
        char status = priceResponse[2];
        
        Serial.printf("\n[RS485 READ] Price Change Response: ");
        for (int i = 0; i < 4; i++) {
          if (i == 2) {
            Serial.printf("'%c'(0x%02X) ", priceResponse[i], priceResponse[i]);
          } else {
            Serial.printf("0x%02X ", priceResponse[i]);
          }
        }
        Serial.println();
        
        // Parse status
        if (status == 'S')
        {
          Serial.printf("[RS485 READ] ✓ SUCCESS - DeviceID=%d price updated successfully\n", deviceId);
          
          // Publish Complete response to MQTT
          if (mqttClient.connected() && deviceId == lastPriceChangeRequest.deviceId)
          {
            // Build response topic: IDChiNhanh/ChangePrice
            char responseTopic[100];
            snprintf(responseTopic, sizeof(responseTopic), "%s/ChangePrice", lastPriceChangeRequest.idChiNhanh);
            
            // Build JSON response
            DynamicJsonDocument doc(512);
            JsonArray array = doc.to<JsonArray>();
            JsonObject entry = array.createNestedObject();
            entry["Key"] = "Complete";
            
            JsonObject item = entry.createNestedObject("item");
            item["IDChiNhanh"] = lastPriceChangeRequest.idChiNhanh;
            item["IdDevice"] = lastPriceChangeRequest.idDevice;
            item["UnitPrice"] = lastPriceChangeRequest.unitPrice;
            
            String jsonString;
            serializeJson(doc, jsonString);
            
            if (mqttClient.publish(responseTopic, jsonString.c_str()))
            {
              Serial.printf("[RS485 READ] ✓ Published Complete to %s: %s\n", responseTopic, jsonString.c_str());
            }
            else
            {
              Serial.printf("[RS485 READ] ✗ Failed to publish Complete to %s\n", responseTopic);
            }
          }
        }
        else if (status == 'E')
        {
          Serial.printf("[RS485 READ] ✗ ERROR - DeviceID=%d rejected price update (KPL returned 'E')\n", deviceId);
          
          // Publish Error response to MQTT
          if (mqttClient.connected() && deviceId == lastPriceChangeRequest.deviceId)
          {
            char responseTopic[100];
            snprintf(responseTopic, sizeof(responseTopic), "%s/ChangePrice", lastPriceChangeRequest.idChiNhanh);
            
            DynamicJsonDocument doc(512);
            JsonArray array = doc.to<JsonArray>();
            JsonObject entry = array.createNestedObject();
            entry["Key"] = "Error";
            
            JsonObject item = entry.createNestedObject("item");
            item["IDChiNhanh"] = lastPriceChangeRequest.idChiNhanh;
            item["IdDevice"] = lastPriceChangeRequest.idDevice;
            item["UnitPrice"] = lastPriceChangeRequest.unitPrice;
            
            String jsonString;
            serializeJson(doc, jsonString);
            
            if (mqttClient.publish(responseTopic, jsonString.c_str()))
            {
              Serial.printf("[RS485 READ] ✓ Published Error to %s: %s\n", responseTopic, jsonString.c_str());
            }
            else
            {
              Serial.printf("[RS485 READ] ✗ Failed to publish Error to %s\n", responseTopic);
            }
          }
        }
        else
        {
          Serial.printf("[RS485 READ] ✗ UNKNOWN STATUS - DeviceID=%d, Status='%c' (0x%02X)\n", 
                        deviceId, status, status);
        }
      }
      else
      {
        Serial.printf("[RS485 READ] Invalid price response format: [0x%02X][0x%02X][0x%02X][0x%02X]\n",
                      priceResponse[0], priceResponse[1], priceResponse[2], priceResponse[3]);
      }
    }
    return;
  }
  
  // Case 2: Pump Log Data - Format: [1][2]...[3][checksum][4] (32 bytes)
  if (firstByte == 1 && Serial2.available() >= LOG_SIZE)
  {
    // Set timeout for reading to prevent blocking
    unsigned long readStartTime = millis();
    size_t bytesRead = Serial2.readBytes(buffer, LOG_SIZE);
    unsigned long readDuration = millis() - readStartTime;
    
    if (readDuration > 1000)
    {
      Serial.printf("[RS485 READ] ⚠️ Read timeout: %lu ms for %d bytes\n", readDuration, bytesRead);
      return;
    }
    
    if (bytesRead == LOG_SIZE)
    {
      // Validate data
      uint8_t calculatedChecksum = calculateChecksum_LogData(buffer, LOG_SIZE);
      uint8_t receivedChecksum = buffer[30];

      if (calculatedChecksum == receivedChecksum &&
          buffer[0] == 1 && buffer[1] == 2 &&
          buffer[29] == 3 && buffer[31] == 4)
      {
        // Process valid pump log data
        PumpLog log;
        ganLog(buffer, log);
        
        // kiểm tra mqttQueue có dữ liệu không
        if (uxQueueMessagesWaiting(mqttQueue) > 0)
        {
          Serial.println("MQTT queue is not empty, skipping...");
          return;
        }

        if (xQueueSend(mqttQueue, &log, pdMS_TO_TICKS(100)) == pdTRUE)
        {
          Serial.println("Log data queued for MQTT");
          // Trigger relay
          xTaskCreate(ConnectedKPLBox, "ConnectedKPLBox", 1024, NULL, 3, NULL);
        }
      }
      else
      {
        Serial.println("[RS485 READ] Invalid pump log: checksum or format error");
      }
    }
    else
    {
      Serial.printf("[RS485 READ] ⚠️ Incomplete read: expected %d bytes, got %d\n", LOG_SIZE, bytesRead);
    }
    return;
  }
  
  // Case 3: Unknown or incomplete data - wait for more data or clear buffer
  if (Serial2.available() > 0 && Serial2.available() < 4)
  {
    // Not enough data yet, wait for next iteration
    return;
  }
  
  // Case 4: Buffer overflow protection - if buffer is suspiciously full, clear it
  if (Serial2.available() > 100)
  {
    Serial.printf("[RS485 READ] ⚠️ Buffer overflow detected (%d bytes), clearing...\n", Serial2.available());
    while (Serial2.available())
    {
      Serial2.read();
    }
    return;
  }
  
  // Case 5: Invalid first byte - clear one byte and try again
  if (firstByte != 1 && firstByte != 7 && firstByte != 9)
  {
    Serial.printf("[RS485 READ] Unknown first byte: 0x%02X, discarding...\n", firstByte);
    Serial2.read(); // Discard invalid byte
  }
}

// viết chương trình resend lại lệnh rs485 với id trong LogIdLossQueue để đọc giá trị lên
// Dựa vào getLogData để resend lại lệnh rs485 với id trong LogIdLossQueue để đọc giá trị lên
// Dựa vào sendLogRequest để resend lại lệnh rs485 với id trong LogIdLossQueue để đọc giá trị lên


void resendLogRequest(void *param)
{
  Serial.println("ResendLogRequest task started");
  esp_task_wdt_add(NULL);
  while (true)
  {
    DtaLogLoss dataLog;
    // kiểm tra xem có dữ liệu trong logIdLossQueue không
    if (uxQueueMessagesWaiting(logIdLossQueue) > 0) 
    {
      if (xQueueReceive(logIdLossQueue, &dataLog, 0) == pdTRUE)
      {
        Serial.println("Resending log request for ID: " + String(dataLog.Logid));
        sendLogRequest(static_cast<uint32_t>(dataLog.Logid));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_task_wdt_reset();
    yield();
  }
}

void sendPriceChangeCommand(const PriceChangeRequest &request)
{
  // Save request for response publishing
  lastPriceChangeRequest = request;
  
  Serial.printf("\n[PRICE CHANGE] Sending command: DeviceID=%d, IdDevice=%s, IDChiNhanh=%s, Price=%.2f\n", 
                request.deviceId, request.idDevice, request.idChiNhanh, request.unitPrice);
  
  // Convert price to 6-digit ASCII string (Char 0-5)
  // Format as integer with leading zeros (6 digits for Char 0-5)
  char priceStr[7];
  snprintf(priceStr, sizeof(priceStr), "%06d", (int)request.unitPrice);
  
  // Build buffer according to protocol (10 bytes total)
  // Protocol: [PM(9)] [ID_Device] [Price_Char0-5 (6 bytes)] [Checksum] [LF(10)]
  uint8_t buffer[10];
  buffer[0] = 9;                 // PM command (Dec 9)
  buffer[1] = request.deviceId;  // ID Device (11-20)
  buffer[2] = priceStr[0];       // Đơn giá Char(0) - ASCII
  buffer[3] = priceStr[1];       // Đơn giá Char(1) - ASCII
  buffer[4] = priceStr[2];       // Đơn giá Char(2) - ASCII
  buffer[5] = priceStr[3];       // Đơn giá Char(3) - ASCII
  buffer[6] = priceStr[4];       // Đơn giá Char(4) - ASCII
  buffer[7] = priceStr[5];       // Đơn giá Char(5) - ASCII
  
  // Calculate checksum: 0x5A XOR all data bytes (ID + Price chars)
  uint8_t checksum = 0x5A ^ buffer[1] ^ buffer[2] ^ buffer[3] ^ buffer[4] ^ buffer[5] ^ buffer[6] ^ buffer[7];
  buffer[8] = checksum;          // Char(Cksum)
  buffer[9] = 10;                // End byte (Dec 10 = LF)
  
  // Send via RS485 (Serial2)
  Serial2.write(buffer, sizeof(buffer));
  Serial2.flush(); // Wait for transmission to complete
  
  Serial.printf("[PRICE CHANGE] ✓ Command sent to KPL: DeviceID=%d, Price=%.2f -> ASCII: %s\n", 
                request.deviceId, request.unitPrice, priceStr);
  Serial.print("[PRICE CHANGE] Data (HEX): ");
  for (int i = 0; i < sizeof(buffer); i++) {
    Serial.printf("0x%02X ", buffer[i]);
  }
  Serial.print("\n[PRICE CHANGE] Data (ASCII): ");
  for (int i = 0; i < sizeof(buffer); i++) {
    if (i >= 2 && i <= 7) {
      Serial.printf("'%c' ", buffer[i]);
    } else {
      Serial.printf("0x%02X ", buffer[i]);
    }
  }
  Serial.println();
  Serial.println("[PRICE CHANGE] Response will be handled by readRS485Data()");
}

void ConnectedKPLBox(void *param)
{
  digitalWrite(OUT1, HIGH);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(OUT2, HIGH);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(OUT1, LOW);
  digitalWrite(OUT2, LOW);
  vTaskDelete(NULL);
}

void BlinkOutput2Task(void *param)
{
  // Two quick blinks on OUT2
  for (int i = 0; i < 1; i++) {
    digitalWrite(OUT1, HIGH);
    digitalWrite(OUT2, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(OUT1, LOW);
    digitalWrite(OUT2, LOW);
    vTaskDelay(pdMS_TO_TICKS(120));
  }
  vTaskDelete(NULL);
}

void blinkOutput2Connected()
{
  // Fire-and-forget non-blocking blink
  xTaskCreate(BlinkOutput2Task, "BlinkOUT2", 1024, NULL, 1, NULL);
}

void wifiRescanTask(void *param)
{
  // Perform a one-time WiFi scan and update list for config portal
  if (wifiManager != nullptr)
  {
    Serial.println("[Rescan] Starting WiFi rescan...");
    wifiManager->scanWiFi();
    Serial.println("[Rescan] WiFi rescan completed.");
  }
  vTaskDelete(NULL);
}

// ============================================================================
// OTA UPDATE FUNCTION
// ============================================================================

// ============================================================================
// OTA UPDATE FROM DIRECT URL (GitHub)
// ============================================================================
void performOTAUpdate(const char* firmwareURL)
{
  Serial.printf("\n\n=== STARTING OTA UPDATE FROM URL ===\n");
  Serial.printf("[OTA INFO] Firmware URL: %s\n", firmwareURL);
  digitalWrite(OUT2, HIGH);

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[OTA ERROR] WiFi not connected.");
    digitalWrite(OUT2, LOW);
    return;
  }
  
  HTTPClient http;
  WiFiClientSecure client; // GitHub requires HTTPS

  client.setInsecure(); // For simplicity
  http.begin(client, firmwareURL);
  
  // GitHub requires these headers
  http.addHeader("User-Agent", "ESP32-OTA-Client");
  http.addHeader("Accept", "application/octet-stream");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // GitHub uses redirects
  http.setTimeout(30000); // 30 seconds timeout

  Serial.println("[OTA INFO] Connecting to server...");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[OTA ERROR] HTTP GET failed. Code: %d, Error: %s\n", httpCode, http.errorToString(httpCode).c_str());
    http.end();
    digitalWrite(OUT2, LOW);
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("[OTA ERROR] Invalid Content-Length.");
    http.end();
    digitalWrite(OUT2, LOW);
    return;
  }

  Serial.printf("[OTA INFO] Firmware size: %d bytes\n", contentLength);

  if (!Update.begin(contentLength))
  {
    Serial.printf("[OTA ERROR] Not enough space for OTA. Error: %s\n", Update.errorString());
    http.end();
    digitalWrite(OUT2, LOW);
    return;
  }

  Serial.println("[OTA INFO] Starting firmware download and flash...");
  
  // Publish OTA started status
  if (mqttClient.connected())
  {
    DynamicJsonDocument doc(256);
    doc["status"] = "OTA_DOWNLOADING";
    doc["progress"] = 0;
    doc["device"] = TopicMqtt;
    String jsonString;
    serializeJson(doc, jsonString);
    mqttClient.publish(topicStatus, jsonString.c_str());
    mqttClient.loop();
  }
  
  // Download with progress tracking
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[512];
  size_t written = 0;
  int lastPercent = -1;
  
  while (http.connected() && written < contentLength)
  {
    size_t available = stream->available();
    if (available)
    {
      size_t len = stream->readBytes(buff, sizeof(buff));
      if (len > 0)
      {
        if (Update.write(buff, len) != len)
        {
          Serial.println("[OTA ERROR] Write failed during download");
          Update.abort();
          http.end();
          digitalWrite(OUT2, LOW);
          
          // Publish failure
          if (mqttClient.connected())
          {
            DynamicJsonDocument doc(256);
            doc["status"] = "OTA_FAILED";
            doc["error"] = "Write failed";
            doc["device"] = TopicMqtt;
            String jsonString;
            serializeJson(doc, jsonString);
            mqttClient.publish(topicStatus, jsonString.c_str());
            mqttClient.loop();
          }
          return;
        }
        written += len;
        
        // Report progress every 5%
        int percent = (written * 100) / contentLength;
        if (percent != lastPercent && percent % 5 == 0)
        {
          Serial.printf("[OTA PROGRESS] %d%% (%u / %d bytes)\n", percent, written, contentLength);
          lastPercent = percent;
          
          // Publish progress to MQTT
          if (mqttClient.connected())
          {
            DynamicJsonDocument doc(256);
            doc["status"] = "OTA_DOWNLOADING";
            doc["progress"] = percent;
            doc["device"] = TopicMqtt;
            String jsonString;
            serializeJson(doc, jsonString);
            mqttClient.publish(topicStatus, jsonString.c_str());
            mqttClient.loop();
          }
        }
      }
    }
    delay(1);
  }

  if (written != contentLength)
  {
    Serial.printf("[OTA ERROR] Download incomplete. Written: %d, Expected: %d\n", written, contentLength);
    Update.abort();
    http.end();
    digitalWrite(OUT2, LOW);
    
    // Publish failure
    if (mqttClient.connected())
    {
      DynamicJsonDocument doc(256);
      doc["status"] = "OTA_FAILED";
      doc["error"] = "Incomplete download";
      doc["device"] = TopicMqtt;
      String jsonString;
      serializeJson(doc, jsonString);
      mqttClient.publish(topicStatus, jsonString.c_str());
      mqttClient.loop();
    }
    return;
  }

  if (!Update.end(true))
  {
    Serial.printf("[OTA ERROR] Finalizing update failed. Error: %s\n", Update.errorString());
    digitalWrite(OUT2, LOW);
    
    // Publish failure
    if (mqttClient.connected())
    {
      DynamicJsonDocument doc(256);
      doc["status"] = "OTA_FAILED";
      doc["error"] = Update.errorString();
      doc["device"] = TopicMqtt;
      String jsonString;
      serializeJson(doc, jsonString);
      mqttClient.publish(topicStatus, jsonString.c_str());
      mqttClient.loop();
    }
    return;
  }

  Serial.println("[OTA SUCCESS] Update successful! Restarting...");
  
  // Publish 100% complete before restart
  if (mqttClient.connected())
  {
    DynamicJsonDocument doc(256);
    doc["status"] = "OTA_SUCCESS";
    doc["progress"] = 100;
    doc["device"] = TopicMqtt;
    String jsonString;
    serializeJson(doc, jsonString);
    mqttClient.publish(topicStatus, jsonString.c_str());
    mqttClient.loop();
  }
  
  delay(1000);
  ESP.restart();
}


// ============================================================================
// OTA UPDATE VIA API FUNCTION (REWRITTEN)
// ============================================================================

void performOTAUpdateViaAPI(const String& apiEndpoint, const String& ftpUrl)
{
  Serial.println("\n\n=== STARTING OTA UPDATE VIA API (v2) ===");
  digitalWrite(OUT2, HIGH); // Turn on OTA indicator LED

  // 1. PRE-FLIGHT CHECKS
  // ===============================================
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[OTA ERROR] WiFi not connected.");
    setSystemStatus("ERROR", "OTA: No WiFi");
    digitalWrite(OUT2, LOW);
    return;
  }

  size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  Serial.printf("[OTA INFO] Free heap before OTA: %u bytes\n", freeHeap);
  if (freeHeap < 60000) // Need enough heap for SSL client and buffers
  {
    Serial.println("[OTA ERROR] Not enough heap memory for OTA update.");
    setSystemStatus("ERROR", "OTA: Low memory");
    digitalWrite(OUT2, LOW);
    return;
  }

  // 2. HTTP CLIENT SETUP
  // ===============================================
  HTTPClient http;
  WiFiClient *streamClient = nullptr;

  if (apiEndpoint.startsWith("https://"))
  {
    WiFiClientSecure *secureClient = new WiFiClientSecure;
    if (secureClient) {
      secureClient->setInsecure(); // For simplicity. In production, consider adding root CA.
      secureClient->setTimeout(20); // 20-second timeout for socket operations
      streamClient = secureClient;
      http.begin(*secureClient, apiEndpoint);
      Serial.println("[OTA INFO] Using HTTPS connection.");
    } else {
      Serial.println("[OTA ERROR] Failed to create secure client.");
      digitalWrite(OUT2, LOW);
      return;
    }
  }
  else
  {
    WiFiClient *client = new WiFiClient;
    streamClient = client;
    http.begin(*client, apiEndpoint);
    Serial.println("[OTA INFO] Using HTTP connection.");
  }
  
  // Set headers to mimic a browser, which is the most reliable configuration we've found
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
  http.addHeader("Accept", "application/json, text/plain, */*");
  http.addHeader("Connection", "close");
  http.setTimeout(60000); // 60-second timeout for the entire request

  // Prepare JSON body
  DynamicJsonDocument postDoc(1024);
  postDoc["FtpUrl"] = ftpUrl.c_str();
  String postBody;
  serializeJson(postDoc, postBody);

  // 3. SEND REQUEST AND CHECK RESPONSE
  // ===============================================
  Serial.printf("[OTA INFO] Sending POST request to %s\n", apiEndpoint.c_str());
  Serial.printf("[OTA INFO] Body: %s\n", postBody.c_str());
  int httpCode = http.POST(postBody);

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("[OTA ERROR] API request failed. HTTP Code: %d\n", httpCode);
    Serial.printf("[OTA ERROR] Server response: %s\n", http.getString().c_str());
    setSystemStatus("ERROR", "OTA: API Error " + String(httpCode));
    http.end();
    digitalWrite(OUT2, LOW);
    return;
  }

  Serial.println("[OTA INFO] API request successful. Receiving firmware...");

  // 4. BEGIN FIRMWARE UPDATE PROCESS
  // ===============================================
  int contentLength = http.getSize();
  Serial.printf("[OTA INFO] Content-Length: %d bytes\n", (contentLength == -1 ? 0 : contentLength));

  bool updateStarted = false;
  if (contentLength > 0) {
    updateStarted = Update.begin(contentLength);
  } else {
    // If size is unknown, start with max size.
    updateStarted = Update.begin(UPDATE_SIZE_UNKNOWN);
  }

  if (!updateStarted)
  {
    Serial.printf("[OTA ERROR] Not enough space for OTA. Error: %s\n", Update.errorString());
    setSystemStatus("ERROR", "OTA: " + String(Update.errorString()));
    http.end();
    digitalWrite(OUT2, LOW);
    return;
  }

  // 5. DOWNLOAD AND FLASH LOOP
  // ===============================================
  Serial.println("[OTA INFO] Starting firmware download and flash...");
  
  // Disable Watchdog Timer for the update process
  disableCore0WDT();
  #if CONFIG_FREERTOS_UNICORE == 0
  disableCore1WDT();
  #endif

  size_t written = 0;
  uint8_t buff[512] = {0};
  unsigned long startTime = millis();
  int lastPercent = -1; // Track progress percentage
  
  WiFiClient* stream = http.getStreamPtr();
  while (http.connected() && (contentLength == -1 || written < contentLength))
  {
    size_t available = stream->available();
    if (available)
    {
      size_t len = stream->readBytes(buff, sizeof(buff));
      if (len > 0)
      {
        if (Update.write(buff, len) != len)
        {
          Serial.printf("[OTA ERROR] Write failed at %u bytes. Error: %s\n", written, Update.errorString());
          
          // CRITICAL DIAGNOSTIC: Dump the failing buffer
          Serial.println("--- BEGIN FAILED BUFFER DUMP (HEX) ---");
          for(int i=0; i<len; i++) {
            Serial.printf("%02X ", buff[i]);
            if ((i+1) % 16 == 0) Serial.println();
          }
          Serial.println("\n--- END FAILED BUFFER DUMP ---");
          
          Update.abort();
          goto ota_end; // Jump to cleanup code
        }
        written += len;

        // Progress reporting
        if (contentLength > 0) {
            int percent = (written * 100) / contentLength;
            if (percent != lastPercent && percent % 5 == 0)
            {
              Serial.printf("[OTA PROGRESS] %d%% (%u / %d bytes)\n", percent, written, contentLength);
              lastPercent = percent;
              
              // Publish progress to MQTT
              if (mqttClient.connected())
              {
                DynamicJsonDocument doc(256);
                doc["status"] = "OTA_DOWNLOADING";
                doc["progress"] = percent;
                doc["device"] = TopicMqtt;
                String jsonString;
                serializeJson(doc, jsonString);
                mqttClient.publish(topicStatus, jsonString.c_str());
                mqttClient.loop();
              }
            }
        } else {
            // Unknown size, report every 50KB
            if (written % 51200 == 0) {
              Serial.printf("[OTA PROGRESS] %u bytes written\n", written);
              
              // Publish progress to MQTT
              if (mqttClient.connected())
              {
                DynamicJsonDocument doc(256);
                doc["status"] = "OTA_DOWNLOADING";
                doc["bytes"] = written;
                doc["device"] = TopicMqtt;
                String jsonString;
                serializeJson(doc, jsonString);
                mqttClient.publish(topicStatus, jsonString.c_str());
                mqttClient.loop();
              }
            }
        }
      }
    }
    // Yield to other tasks and feed WDT
    vTaskDelay(1); 
  }

ota_end:
  // 6. FINALIZE AND CLEANUP
  // ===============================================
  unsigned long duration = (millis() - startTime) / 1000;
  Serial.printf("\n[OTA INFO] Download finished in %lu seconds. Total bytes: %u\n", duration, written);

  // Re-enable Watchdog Timer
  enableCore0WDT();
  #if CONFIG_FREERTOS_UNICORE == 0
  enableCore1WDT();
  #endif
  
  http.end();
  digitalWrite(OUT2, LOW);

  if (Update.hasError())
  {
    Serial.printf("[OTA ERROR] Update failed with error: %s\n", Update.errorString());
    setSystemStatus("ERROR", "OTA: " + String(Update.errorString()));
  }
  else if (Update.isFinished())
  {
    if (Update.end(true)) // true to set the new partition as bootable
    {
      Serial.println("[OTA SUCCESS] Update successful! Restarting...");
      delay(1000);
      ESP.restart();
    }
    else
    {
      Serial.printf("[OTA ERROR] Finalizing update failed. Error: %s\n", Update.errorString());
      setSystemStatus("ERROR", "OTA: Finalize failed");
    }
  }
  else
  {
      Serial.println("[OTA ERROR] Update did not complete.");
      setSystemStatus("ERROR", "OTA: Incomplete");
  }
  
  Serial.println("=== OTA UPDATE VIA API PROCESS COMPLETED ===");
}