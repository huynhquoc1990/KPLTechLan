#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

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

// FreeRTOS objects
static QueueHandle_t mqttQueue = NULL;
static QueueHandle_t logIdLossQueue = NULL;
static SemaphoreHandle_t flashMutex = NULL;
static SemaphoreHandle_t systemMutex = NULL;

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

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================



void setup()
{
  Serial.begin(115200);
  Serial.println("\n=== KPL Gas Device Starting ===");
  
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

  // Watchdog setup
  esp_task_wdt_init(60, true); // Increase timeout to 60s for safety during WiFi ops
  esp_task_wdt_add(NULL);

  // Initialize RS485
  Serial2.begin(RS485BaudRate, SERIAL_8N1, RX_PIN, TX_PIN);

  // Initialize FreeRTOS objects
  mqttQueue = xQueueCreate(10, sizeof(PumpLog));
  logIdLossQueue = xQueueCreate(50, sizeof(DtaLogLoss));
  flashMutex = xSemaphoreCreateMutex();
  systemMutex = xSemaphoreCreateMutex();

  if (!mqttQueue || !logIdLossQueue || !flashMutex || !systemMutex)
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

  byte buffer[LOG_SIZE];
  unsigned long lastSendTime = 0;
  // esp_task_wdt_add(NULL);
  
  while (true)
  {
    // Read RS485 data
    readRS485Data(buffer);

    // Send log requests every 2 seconds (reduce heat/power)
    if (millis() - lastSendTime >= 2000)
    {
      lastSendTime = millis();
      // Process log loss queue
      DtaLogLoss dataLog;
      if (xQueueReceive(logIdLossQueue, &dataLog, 0) == pdTRUE)
      {
        sendLogRequest(static_cast<uint32_t>(dataLog.Logid));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    // esp_task_wdt_reset();
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

    Serial.printf("Subscription results:\n");
    Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
    Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
    Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
    Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
    Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
    
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

      Serial.printf("Re-subscription results:\n");
      Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
      Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
      Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
      Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
      Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
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
  if (strcmp(topic, topicRestart) == 0)
  {
    Serial.println("Restart command received - restarting ESP32...");
    ESP.restart();
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

  if (Serial2.readBytes(buffer, LOG_SIZE) == LOG_SIZE)
  {
    // Validate data
    uint8_t calculatedChecksum = calculateChecksum_LogData(buffer, LOG_SIZE);
    uint8_t receivedChecksum = buffer[30];

    if (calculatedChecksum == receivedChecksum &&
        buffer[0] == 1 && buffer[1] == 2 &&
        buffer[29] == 3 && buffer[31] == 4)
    {

      // Process valid data
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
    digitalWrite(OUT2, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
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