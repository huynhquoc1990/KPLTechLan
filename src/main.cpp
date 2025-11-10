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
#include <esp_partition.h> // CRITICAL: Added for partition info

// ============================================================================
// DEBUG LOGGING MACROS
// ============================================================================
#ifdef DEBUG_MODE
  #define DEBUG_PRINT(x)       Serial.print(x)
  #define DEBUG_PRINTLN(x)     Serial.println(x)
  #define DEBUG_PRINTF(...)    Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)       // No-op in release mode
  #define DEBUG_PRINTLN(x)     // No-op in release mode
  #define DEBUG_PRINTF(...)    // No-op in release mode
#endif

// Critical logs (always enabled, even in release mode)
#define LOG_ERROR(x)           Serial.println(x)
#define LOG_ERROR_F(...)       Serial.printf(__VA_ARGS__)

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

// Nozzle prices storage
static NozzlePrices nozzlePrices;

// System state
static uint32_t currentId = 0;
static bool statusConnected = false;
static bool isLoggedIn = false;
static bool mqttTopicsConfigured = false;
static bool mqttSubscribed = false; // Track subscription state
static unsigned long counterReset = 0;
static unsigned long lastHeapCheck = 0;
static char systemStatus[32] = "OK";
static char lastError[128] = "";

// CRITICAL: Global flag to disable flash writes on old partition
static bool g_flashSaveEnabled = true;

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
static char topicUpdatePrice[64];     // topic for changing price
static char topicGetPrice[64];        // topic for requesting current prices
static char topicRequestLog[64];      // topic for requesting logs from Flash

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
void setSystemStatus(const char* status, const char* error = "");
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
void printPartitionInfo(); // CRITICAL: Added forward declaration
bool validateMacWithServer(const char* macAddress); // SECURITY: MAC validation

// ============================================================================
// MAIN SETUP & LOOP
// ============================================================================

// dùng để kiểm tra có phát sinh giao dịch ko, nếu có reset biến này. và 60 phút nếu không có giao dịch thì restart esp
unsigned long checkLogSend = 0;


void setup()
{
  Serial.begin(115200);
  while (!Serial); // Wait for serial connection

  Serial.println("\n\n=== KPL Techlan Device Booting ===");

  // CRITICAL: Print partition info right at the start
  printPartitionInfo();

  // Read reset reason
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d - ", reason);
  switch(reason)
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
  
  // SECURITY: Validate MAC with server
  if (!validateMacWithServer(macStr)) {
    Serial.println("❌ DEVICE NOT AUTHORIZED!");
    Serial.println("MAC address not found in authorized devices list.");
    Serial.println("Contact KPL Tech support: 0xxx-xxx-xxx");
    
    // Brick device - infinite loop
    while (true) {
      digitalWrite(OUT1, HIGH);
      digitalWrite(OUT2, HIGH);
      delay(500);
      digitalWrite(OUT1, LOW);
      digitalWrite(OUT2, LOW);
      delay(500);
      Serial.println("UNAUTHORIZED DEVICE - Contact support");
      delay(30000);  // Print every 30 seconds
    }
  }
  
  Serial.println("✓ Device authorized by server");

  // call api để check mac có trong hệ thống hay không, sử dụng Phương thức Poss

  String url = String(API_BASE_URL) + API_VALIDATE_MAC_ENDPOINT;
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
  flashMutex = xSemaphoreCreateMutex();
  systemMutex = xSemaphoreCreateMutex();
  mqttQueue = xQueueCreate(300, sizeof(PumpLog)); // Increased from 5
  logIdLossQueue = xQueueCreate(300, sizeof(DtaLogLoss)); // CRITICAL: Increased from 50 to 500
  priceChangeQueue = xQueueCreate(10, sizeof(PriceChangeRequest));

  if (flashMutex == NULL || systemMutex == NULL || mqttQueue == NULL || logIdLossQueue == NULL || priceChangeQueue == NULL)
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
  
  // Load nozzle prices from Flash
  Serial.println("Loading nozzle prices from Flash...");
  if (loadNozzlePrices(nozzlePrices, flashMutex)) {
    printNozzlePrices(nozzlePrices);
  } else {
    Serial.println("No saved prices found, initializing with defaults (0.0)");
    // Save initial default prices
    saveNozzlePrices(nozzlePrices, flashMutex);
  }

  // Initialize WiFi
  WiFi.mode(WIFI_STA);

  // Initialize MQTT - will be updated from API settings
  mqttClient.setServer(mqttServer, 1883); // Default server, will be updated from API
  mqttClient.setBufferSize(24576); // 24KB buffer for compressed log responses (up to 200 logs)
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
  static unsigned long lastQueueCheck = 0;
  unsigned long now = millis();

  // Check every 10 seconds
  if (now - lastCheck >= 10000)
  {
    lastCheck = now;

    // Reset watchdog
    esp_task_wdt_reset();

    // Check heap memory
    checkHeap();

    // Queue overflow monitoring (every 30 seconds)
    if (now - lastQueueCheck >= 30000)
    {
      lastQueueCheck = now;
      
      UBaseType_t mqttQueueCount = uxQueueMessagesWaiting(mqttQueue);
      if (mqttQueueCount > 8) // 80% of 10
      {
        Serial.printf("⚠️ MQTT queue nearly full: %d/10\n", mqttQueueCount);
        setSystemStatus("WARNING", "MQTT queue overload");
      }
      
      UBaseType_t logLossQueueCount = uxQueueMessagesWaiting(logIdLossQueue);
      if (logLossQueueCount > 40) // 80% of 50
      {
        Serial.printf("⚠️ LogLoss queue nearly full: %d/50\n", logLossQueueCount);
        setSystemStatus("WARNING", "LogLoss queue overload");
      }
      
      UBaseType_t priceQueueCount = uxQueueMessagesWaiting(priceChangeQueue);
      if (priceQueueCount > 8) // 80% of 10
      {
        Serial.printf("⚠️ Price change queue nearly full: %d/10\n", priceQueueCount);
        setSystemStatus("WARNING", "Price queue overload");
      }
    }

    // Safe auto-restart after 60 minutes of no logs
    checkLogSend++;
    if (checkLogSend >= 360) // 360 * 10s = 3600s = 60 minutes
    {
      Serial.printf("No logs received for 60 minutes (%lu checks) - initiating safe restart...\n", checkLogSend);
      
      // Check if safe to restart
      if (Update.isRunning()) {
        Serial.println("⚠️ OTA in progress - postponing restart");
        checkLogSend = 350; // Retry in 100 seconds
        return;
      }
      
      if (uxQueueMessagesWaiting(mqttQueue) > 0) {
        Serial.printf("⚠️ MQTT queue has %d pending logs - postponing restart\n", 
                      uxQueueMessagesWaiting(mqttQueue));
        checkLogSend = 350; // Retry in 100 seconds
        return;
      }
      
      // Additional check: Don't restart if logIdLossQueue has pending logs
      if (uxQueueMessagesWaiting(logIdLossQueue) > 0) {
        Serial.printf("⚠️ LogLoss queue has %d pending requests - postponing restart\n", 
                      uxQueueMessagesWaiting(logIdLossQueue));
        checkLogSend = 350; // Retry in 100 seconds
        return;
      }
      
      if (xSemaphoreTake(flashMutex, 0) != pdTRUE) {
        Serial.println("⚠️ Flash is busy - postponing restart");
        checkLogSend = 350; // Retry in 100 seconds
        return;
      }
      xSemaphoreGive(flashMutex);
      
      // Save counter to Flash before restart
      Serial.println("✓ System is safe to restart - saving state...");
      counterReset++;
      if (writeResetCountToFlash(flashMutex, counterReset)) {
        Serial.printf("✓ Counter saved: %lu\n", counterReset);
      } else {
        Serial.println("⚠️ Failed to save counter, restarting anyway...");
      }
      
      Serial.println("✓ Restarting in 3 seconds...");
      delay(3000);
      ESP.restart();
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

  // Log memory status (debug only)
  DEBUG_PRINTF("Heap: %u free, %u min free, Temp: %.1f°C\n",
                freeHeap, minFreeHeap, deviceStatus.temperature);

  // Memory warning (always log critical errors)
  if (freeHeap < 10000)
  {
    LOG_ERROR("WARNING: Low memory!");
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "Low memory: %u bytes", freeHeap);
    setSystemStatus("WARNING", errorMsg);
  }
  else if (strcmp(systemStatus, "WARNING") == 0 && strstr(lastError, "Low memory") != NULL)
  {
    // Clear memory warning if heap is back to normal
    setSystemStatus("OK", "");
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

void setSystemStatus(const char* status, const char* error)
{
  strncpy(systemStatus, status, sizeof(systemStatus) - 1);
  systemStatus[sizeof(systemStatus) - 1] = '\0';
  
  if (error && strlen(error) > 0) {
    strncpy(lastError, error, sizeof(lastError) - 1);
    lastError[sizeof(lastError) - 1] = '\0';
  } else {
    lastError[0] = '\0';
  }

  if (strcmp(status, "OK") != 0)
  {
    Serial.printf("[STATUS] %s: %s\n", status, error ? error : "");
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
  // CRITICAL: Initial delay after boot to give router time to start
  // This handles the case when both device and router lose power simultaneously
  static bool firstBoot = true;
  if (firstBoot) {
    Serial.println("[WiFi] Initial boot delay: waiting 5 seconds for router to start...");
    for (int i = 0; i < 5; i++) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    firstBoot = false;
  }

  while (true)
  {
    esp_task_wdt_reset();
    if (WiFi.status() != WL_CONNECTED)
    {
      // Check if router is available before attempting connection
      // This prevents connection attempts when router is still booting
      Serial.println("[WiFi] Checking if router is available...");
      bool routerAvailable = wifiManager->isRouterAvailable();
      
      if (!routerAvailable) {
        Serial.println("[WiFi] Router not detected. Waiting before retry...");
        // Router not available - wait and retry scan
        // Don't turn WiFi completely off, just wait and scan again
        for (int i = 0; i < 5; i++) { // Wait 5 seconds
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
        continue; // Retry router check
      }
      
      Serial.println("[WiFi] Router detected. Attempting to connect...");
      static uint32_t cooldownSeconds = 10; // exponential backoff, capped
      static uint32_t failedAttempts = 0;
      
      if (wifiManager->connectToWiFi())
      {
        Serial.println("WiFi connected successfully.");
        // Reset backoff counters on successful connection
        cooldownSeconds = 10;
        failedAttempts = 0;
        
        setupTime();
        
        // Only setup MQTT topics if not already configured
        if (!mqttTopicsConfigured)
        {
          Serial.println("Setting up MQTT topics for first time...");
          setupMQTTTopics();
        }
        else
        {
          Serial.println("MQTT topics already configured, skipping setup");
        }
        
        readMacEsp();
        statusConnected = true;
      }
      else
      {
        failedAttempts++;
        
        Serial.printf("[WiFi] Connection failed (attempt %d), cooling radio for %lus...\n", 
                     failedAttempts, cooldownSeconds);
        setSystemStatus("ERROR", "WiFi connection failed");

        // Reset MQTT state when WiFi fails
        mqttTopicsConfigured = false;
        statusConnected = false;
        
        // Unsubscribe from MQTT topics before disconnecting
        if (mqttClient.connected() && mqttSubscribed)
        {
          Serial.println("WiFi lost - unsubscribing from MQTT topics...");
          mqttClient.unsubscribe(topicErrorSub);
          mqttClient.unsubscribe(topicRestart);
          mqttClient.unsubscribe(topicGetLogIdLoss);
          mqttClient.unsubscribe(topicChange);
          mqttClient.unsubscribe(topicShift);
          mqttClient.unsubscribe(topicOTA);
          mqttClient.unsubscribe(topicUpdatePrice);
          mqttClient.disconnect();
          mqttSubscribed = false;
          Serial.println("MQTT topics unsubscribed and disconnected");
        }

        // IMPROVED: During cooldown, periodically check if router becomes available
        // Instead of turning WiFi completely off, we keep it in STA mode and scan
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA); // Keep in STA mode (not WIFI_OFF)
        WiFi.setSleep(true);
        setCpuFrequencyMhz(160);
        
        // During cooldown, check router availability every 3 seconds
        // If router becomes available, reset backoff and try immediately
        for (uint32_t i = 0; i < cooldownSeconds; i++) {
          esp_task_wdt_reset();
          
          // Check router every 3 seconds during cooldown
          if (i % 3 == 0 && i > 0) {
            Serial.printf("[WiFi] Cooldown: checking router availability (%d/%d)...\n", 
                         i, cooldownSeconds);
            if (wifiManager->isRouterAvailable()) {
              Serial.println("[WiFi] Router detected during cooldown! Resetting backoff and retrying...");
              cooldownSeconds = 5; // Reset to minimum
              failedAttempts = 0;
              setCpuFrequencyMhz(240);
              break; // Exit cooldown loop and retry connection
            }
          }
          
          vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // Restore for next attempt
        WiFi.setSleep(true);
        setCpuFrequencyMhz(240);

        // Increase backoff up to 60s (only if connection still failed)
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
      
      // Log WiFi/MQTT status periodically
      static unsigned long lastStatusLog = 0;
      if (millis() - lastStatusLog > 60000) // Every 60 seconds
      {
        Serial.printf("WiFi Status: %s, MQTT Topics: %s, MQTT Connected: %s\n", 
                     WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED",
                     mqttTopicsConfigured ? "CONFIGURED" : "NOT_CONFIGURED",
                     mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");
        lastStatusLog = millis();
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
  // esp_task_wdt_add(NULL); // CRITICAL: Add task to WDT monitoring

  while (true)
  {
    // CRITICAL: Feed WDT at start of each cycle to prevent timeout
    esp_task_wdt_reset();
    
    if (WiFi.status() == WL_CONNECTED)
    {
      // Wait for MQTT topics to be configured before attempting connection
      if (!mqttTopicsConfigured)
      {
        Serial.println("MQTT topics not configured yet, waiting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
        continue;
      }
      
      if (!mqttClient.connected())
      {
        connectMQTT();
        esp_task_wdt_reset(); // Reset after potentially long connection attempt
      }
      else
      {
        // Process MQTT queue
        PumpLog log;
        if (xQueueReceive(mqttQueue, &log, pdMS_TO_TICKS(10)) == pdTRUE)
        {
          Serial.printf("Processing log %d\n", log.viTriLogData);
          sendMQTTData(log);
          esp_task_wdt_reset(); // Reset after processing each log
        }
        
        // Additional loop call to ensure callback processing
        mqttClient.loop();
      }
    }
    else
    {
      // WiFi disconnected - unsubscribe and disconnect MQTT
      if (mqttClient.connected() && mqttSubscribed)
      {
        Serial.println("WiFi disconnected - cleaning up MQTT subscriptions...");
        mqttClient.unsubscribe(topicErrorSub);
        mqttClient.unsubscribe(topicRestart);
        mqttClient.unsubscribe(topicGetLogIdLoss);
        mqttClient.unsubscribe(topicChange);
        mqttClient.unsubscribe(topicShift);
        mqttClient.unsubscribe(topicOTA);
        mqttClient.unsubscribe(topicUpdatePrice);
        mqttClient.unsubscribe(topicGetPrice);
        mqttClient.unsubscribe(topicRequestLog);
        mqttClient.disconnect();
        mqttSubscribed = false;
        Serial.println("MQTT cleanup completed");
      }
    }

    // System monitoring (every 10 seconds)
    systemCheck();

    vTaskDelay(pdMS_TO_TICKS(100));
    // esp_task_wdt_reset(); // Reset at end of cycle
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
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "Failed to sync time after %d attempts", maxRetries);
    setSystemStatus("WARNING", errorMsg);
  }
}

void setupMQTTTopics()
{
  // Get MQTT settings from API
  callAPIGetSettingsMqtt(&settings, flashMutex);
  
  // Update MQTT server with settings from API
  if (strlen(settings.MqttServer) > 0)
  {
    // Disconnect if currently connected to force reconnection with new server
    if (mqttClient.connected())
    {
      mqttClient.disconnect();
      Serial.println("MQTT disconnected to update server settings");
    }
    
    mqttClient.setServer(settings.MqttServer, settings.PortMqtt);
    // Serial.printf("MQTT server updated from API: %s:%d\n", settings.MqttServer, settings.PortMqtt);
  }
  else
  {
    Serial.println("Using default MQTT server (no API settings)");
  }
  
  // Get company info
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
  
  snprintf(topicUpdatePrice, sizeof(topicUpdatePrice), "%s%s", companyInfo.CompanyId, TopicUpdatePrice);
  snprintf(topicGetPrice, sizeof(topicGetPrice), "%s%s", companyInfo.Mst, TopicGetPrice);
  snprintf(topicRequestLog, sizeof(topicRequestLog), "%s%s", companyInfo.CompanyId, TopicRequestLog);
  // snprintf(topicUpdatePrice, sizeof(topicUpdatePrice), "%s%s%s", companyInfo.CompanyId, TopicUpdatePrice);

  Serial.printf("MQTT topics configured - Company ID: %s (MST: %s)\n", companyInfo.CompanyId, companyInfo.Mst);
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
    bool sub7 = mqttClient.subscribe(topicUpdatePrice);
    bool sub8 = mqttClient.subscribe(topicGetPrice);
    bool sub9 = mqttClient.subscribe(topicRequestLog);

    Serial.printf("Subscription results:\n");
    Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
    Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
    Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
    Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
    Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
    Serial.printf("  OTA (%s): %s\n", topicOTA, sub6 ? "SUCCESS" : "FAILED");
    Serial.printf("  UpdatePrice (%s): %s\n", topicUpdatePrice, sub7 ? "SUCCESS" : "FAILED");
    Serial.printf("  GetPrice (%s): %s\n", topicGetPrice, sub8 ? "SUCCESS" : "FAILED");
    Serial.printf("  RequestLog (%s): %s\n", topicRequestLog, sub9 ? "SUCCESS" : "FAILED");
    Serial.println("=== SUBSCRIPTION COMPLETE ===");
    
    // Set subscription flag
    mqttSubscribed = (sub1 && sub2 && sub3 && sub4 && sub5 && sub6 && sub7 && sub8 && sub9);
    Serial.printf("MQTT subscription state: %s\n", mqttSubscribed ? "SUBSCRIBED" : "PARTIAL_FAILURE");
      }
      else
      {
    Serial.println("MQTT not connected, will subscribe when connected");
  }
}

void connectMQTT()
{
  static uint32_t mqttBackoffSeconds = 5; // Exponential backoff for MQTT reconnection
  
  Serial.println("Connecting to MQTT...");
  
  // Set connection timeout
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  if (mqttClient.connect(TopicMqtt, mqttUser, mqttPassword))
  {
    Serial.println("MQTT connected");
    statusConnected = true;
    mqttBackoffSeconds = 5; // Reset backoff on successful connection
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
      bool sub7 = mqttClient.subscribe(topicUpdatePrice);
      bool sub8 = mqttClient.subscribe(topicGetPrice);
      bool sub9 = mqttClient.subscribe(topicRequestLog);

      Serial.printf("Re-subscription results:\n");
      Serial.printf("  Error (%s): %s\n", topicError, sub1 ? "SUCCESS" : "FAILED");
      Serial.printf("  Restart (%s): %s\n", topicRestart, sub2 ? "SUCCESS" : "FAILED");
      Serial.printf("  GetLogIdLoss (%s): %s\n", topicGetLogIdLoss, sub3 ? "SUCCESS" : "FAILED");
      Serial.printf("  Change (%s): %s\n", topicChange, sub4 ? "SUCCESS" : "FAILED");
      Serial.printf("  Shift (%s): %s\n", topicShift, sub5 ? "SUCCESS" : "FAILED");
      Serial.printf("  OTA (%s): %s\n", topicOTA, sub6 ? "SUCCESS" : "FAILED");
      Serial.printf("  UpdatePrice (%s): %s\n", topicUpdatePrice, sub7 ? "SUCCESS" : "FAILED");
      Serial.printf("  GetPrice (%s): %s\n", topicGetPrice, sub8 ? "SUCCESS" : "FAILED");
      Serial.printf("  RequestLog (%s): %s\n", topicRequestLog, sub9 ? "SUCCESS" : "FAILED");
      Serial.println("=== RE-SUBSCRIPTION COMPLETE ===");
      
      // Set subscription flag
      mqttSubscribed = (sub1 && sub2 && sub3 && sub4 && sub5 && sub6 && sub7 && sub8 && sub9);
      Serial.printf("MQTT re-subscription state: %s\n", mqttSubscribed ? "SUBSCRIBED" : "PARTIAL_FAILURE");
      
      // Publish saved prices from Flash after successful MQTT connection
      Serial.println("\n=== PUBLISHING SAVED PRICES FROM FLASH ===");
      publishSavedPricesToMQTT(nozzlePrices, mqttClient, companyInfo.CompanyId, 
                              companyInfo.Mst, TopicMqtt);
      Serial.println("=== SAVED PRICES PUBLISHED ===\n");
    }
        }
        else
        {
    Serial.printf("MQTT connection failed. State: %d\n", mqttClient.state());
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "MQTT failed - state: %d", mqttClient.state());
    setSystemStatus("ERROR", errorMsg);
    
    // Exponential backoff: 5s → 10s → 20s → 40s → 80s → 160s → max 300s
    Serial.printf("MQTT connection failed - cooling down for %lus...\n", mqttBackoffSeconds);
    vTaskDelay(pdMS_TO_TICKS(mqttBackoffSeconds * 1000));
    
    if (mqttBackoffSeconds < 300) {
      mqttBackoffSeconds = mqttBackoffSeconds < 150 ? mqttBackoffSeconds * 2 : 300;
    }
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
  
  // CRITICAL: Add partition warning flag
  doc["oldPartition"] = !g_flashSaveEnabled;
  if (!g_flashSaveEnabled) {
    doc["warning"] = "OLD_PARTITION_FLASH_REQUIRED";
  }

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
  DEBUG_PRINTF("=== MQTT CALLBACK TRIGGERED ===\n");
  DEBUG_PRINTF("Topic: %s\n", topic);
  
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
    Serial.printf("Topic: %s\n", topic);
    Serial.printf("Payload length: %d\n", length);
    Serial.print("Payload: ");
    for (unsigned int i = 0; i < length && i < 200; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println();
    
    parsePayload_IdLogLoss(payload, length, &receivedMessage, &companyInfo);
    Serial.printf("Parsed Idvoi: %s, Expected: %s\n", receivedMessage.Idvoi, TopicMqtt);

    if (strcmp(receivedMessage.Idvoi, TopicMqtt) == 0)
    {
      Serial.println("Idvoi matches - processing log loss...");
      
      UBaseType_t queueSize = uxQueueMessagesWaiting(logIdLossQueue);
      Serial.printf("LogIdLossQueue size: %d\n", queueSize);
      
        if (queueSize == 0)
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
        if (xTaskCreate(callAPIServerGetLogLoss, "GetData", 32768, taskParams, 3, NULL) != pdPASS)
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
  if (strcmp(topic, topicUpdatePrice) == 0)
  {
    DEBUG_PRINTF("[MQTT] UpdatePrice command received - parsing payload...\n");
    DEBUG_PRINTF("[MQTT] Current device MST: %s\n", companyInfo.Mst);
    DEBUG_PRINTF("[MQTT] Payload length: %d bytes\n", length);
    
    // Sync time from NTP before updating price to ensure accurate timestamp
    Serial.println("[MQTT] Syncing time from NTP server...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait a bit for time sync (non-blocking)
    struct tm timeinfo;
    int syncRetry = 0;
    while (!getLocalTime(&timeinfo) && syncRetry < 5)
    {
      vTaskDelay(pdMS_TO_TICKS(200));
      syncRetry++;
    }
    
    if (syncRetry < 5)
    {
      Serial.printf("[MQTT] ✓ Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                   timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    else
    {
      Serial.println("[MQTT] ⚠️ Time sync timeout, using system time");
    }
    
    // Print raw payload for debugging (debug only)
    DEBUG_PRINT("[MQTT] Raw payload: ");
    for (size_t i = 0; i < length && i < 500; i++) {
      DEBUG_PRINT((char)payload[i]);
    }
    DEBUG_PRINTLN("");
    
    // Parse JSON payload with new structure: {"topic":"...", "clientid":"...", "message":[...]}
    DynamicJsonDocument doc(2048); // Increase size for nested structure
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error)
    {
      Serial.printf("[MQTT] UpdatePrice: JSON parse error: %s\n", error.c_str());
      setSystemStatus("ERROR", "UpdatePrice: Invalid JSON payload");
      return;
    }
    
    // Check if root is an object
    if (!doc.is<JsonObject>())
    {
      Serial.println("[MQTT] UpdatePrice: Payload is not a JSON object");
      setSystemStatus("ERROR", "UpdatePrice: Expected JSON object");
      return;
    }
    
    // Extract message array from the payload
    JsonArray priceArray = doc["message"];
    if (priceArray.isNull())
    {
      Serial.println("[MQTT] UpdatePrice: Missing 'message' field");
      setSystemStatus("ERROR", "UpdatePrice: Missing 'message' array");
      return;
    }
    
    Serial.printf("[MQTT] UpdatePrice: Received %d price entries\n", priceArray.size());
    
    // Parse and queue each price change request for RS485 task to process
    // Each device will only process messages that match its own IDChiNhanh and IdDevice
    int queued = 0;
    int skipped = 0;
    
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
      const char* nozzorle = item["Nozzorle"] | "";
      
      // Check if this is for current company (compare IDChiNhanh with MST)
      if (strlen(idChiNhanh) > 0 && strcmp(idChiNhanh, companyInfo.Mst) != 0)
      {
        DEBUG_PRINTF("[MQTT] Skipping - IDChiNhanh=%s doesn't match MST=%s\n", idChiNhanh, companyInfo.Mst);
        skipped++;
        continue;
      }
      
      DEBUG_PRINTF("[MQTT] ✅ Processing Entry: IDChiNhanh=%s, IdDevice=%s, Nozzorle=%s\n", idChiNhanh, idDevice, nozzorle);
      
      // Handle null UnitPrice
      if (item["UnitPrice"].isNull())
      {
        Serial.println("[MQTT] UnitPrice is null, skipping...");
        skipped++;
        continue;
      }
      
      float unitPrice = item["UnitPrice"] | 0.0f;
      DEBUG_PRINTF("[MQTT] UnitPrice=%.2f\n", unitPrice);
      
      // Use Nozzorle field as RS485 Device ID directly
      // Nozzorle is provided as string (e.g., "11", "12", "13", ..., "20")
      if (strlen(nozzorle) == 0)
      {
        Serial.printf("[MQTT] Error: Missing Nozzorle field for IdDevice=%s, skipping...\n", idDevice);
        skipped++;
        continue;
      }
      
      // Parse Nozzorle to integer (already RS485 Device ID)
      uint8_t deviceIdNum = atoi(nozzorle);
      
      // Validate range (11-20)
      if (deviceIdNum < 11 || deviceIdNum > 20)
      {
        Serial.printf("[MQTT] Invalid Nozzorle: %s (must be 11-20), skipping...\n", nozzorle);
        skipped++;
        continue;
      }
      
      DEBUG_PRINTF("[MQTT] Nozzorle=%s -> RS485 DeviceID=%d\n", nozzorle, deviceIdNum);
      
      // Create price change request for this specific pump
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
        Serial.printf("[MQTT] ✓ Queued price change: IdDevice=%s -> PumpID=%d, Price=%.2f\n", idDevice, deviceIdNum, unitPrice);
      }
      else
      {
        skipped++;
        Serial.printf("[MQTT] ✗ Failed to queue: Queue full for PumpID=%d\n", deviceIdNum);
      }
    }
    
    // Send summary
    Serial.printf("\n[MQTT] ChangePrice Summary: Queued=%d, Skipped=%d, Total=%d\n", 
                  queued, skipped, priceArray.size());
    Serial.printf("[MQTT] RS485 task will process %d price change(s)\n", queued);
    
    if (queued > 0) {
      char statusMsg[64];
      snprintf(statusMsg, sizeof(statusMsg), "Queued %d price change(s)", queued);
      setSystemStatus("OK", statusMsg);
    } else {
      setSystemStatus("WARNING", "No price changes queued");
    }
  }
  
  // Handle GetPrice command - Request current prices from Flash
  if (strcmp(topic, topicGetPrice) == 0)
  {
    Serial.println("[MQTT] GetPrice command received - reading prices from Flash...");
    
    // Load latest prices from Flash
    NozzlePrices currentPrices;
    if (!loadNozzlePrices(currentPrices, flashMutex))
    {
      Serial.println("[MQTT] ✗ Failed to load prices from Flash");
      setSystemStatus("ERROR", "Failed to load prices from Flash");
      return;
    }
    
    Serial.println("[MQTT] ✓ Prices loaded from Flash, publishing...");
    
    // Build response topic: {IdChiNhanh}/ResponsePrice
    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "%s/ResponsePrice", companyInfo.Mst);
    
    // Create JSON response with all nozzle prices
    DynamicJsonDocument doc(2048); // Increased size for 10 nozzles with all fields
    doc["topic"] = companyInfo.CompanyId;
    doc["clientid"] = TopicMqtt;
    doc["timestamp"] = currentPrices.lastUpdate;
    
    // Create prices array for all 10 nozzles (11-20)
    JsonArray pricesArray = doc.createNestedArray("prices");
    for (int i = 0; i < 10; i++)
    {
      JsonObject nozzle = pricesArray.createNestedObject();
      nozzle["Nozzle"] = currentPrices.nozzles[i].nozzorle[0] ? currentPrices.nozzles[i].nozzorle : String(11 + i);
      nozzle["IdDevice"] = currentPrices.nozzles[i].idDevice;
      nozzle["UnitPrice"] = currentPrices.nozzles[i].price;
      
      // Format timestamp to dd/mm/yyyy-HH:MM:SS
      time_t timestamp = currentPrices.nozzles[i].updatedAt;
      if (timestamp > 0) {
        struct tm *timeinfo = localtime(&timestamp);
        char formattedTime[32];
        snprintf(formattedTime, sizeof(formattedTime), "%02d/%02d/%04d-%02d:%02d:%02d",
                 timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        nozzle["UpdatedAt"] = formattedTime;
      } else {
        nozzle["UpdatedAt"] = "N/A";  // Chưa có giá trị
      }
    }
    
    // Serialize and publish
    String jsonString;
    serializeJson(doc, jsonString);
    
    if (mqttClient.publish(responseTopic, jsonString.c_str()))
    {
      Serial.printf("[MQTT] ✓ Published ResponsePrice to %s\n", responseTopic);
      Serial.printf("[MQTT] Payload: %s\n", jsonString.c_str());
      setSystemStatus("OK", "Price data published");
    }
    else
    {
      Serial.printf("[MQTT] ✗ Failed to publish ResponsePrice to %s\n", responseTopic);
      setSystemStatus("ERROR", "Failed to publish price data");
    }
  }

  // Handle RequestLog command - Request specific logs from Flash
  if (strcmp(topic, topicRequestLog) == 0)
  {
    DEBUG_PRINTLN("[MQTT] RequestLog command received - parsing payload...");
    
    // Parse JSON payload: {"Mst": "...", "IdDevice": "...", "BeginLog": 1, "Numslog": 10}
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error)
    {
      Serial.printf("[MQTT] RequestLog: JSON parse error: %s\n", error.c_str());
      setSystemStatus("ERROR", "RequestLog: Invalid JSON payload");
      return;
    }
    
    // Extract required fields
    const char* mst = doc["Mst"] | "";
    const char* idDevice = doc["IdDevice"] | "";
    uint16_t beginLog = doc["BeginLog"] | 0;
    uint16_t numsLog = doc["Numslog"] | 0;
    
    // Validate MST and IdDevice
    if (strlen(mst) == 0 || strcmp(mst, companyInfo.Mst) != 0)
    {
      DEBUG_PRINTF("[MQTT] RequestLog: MST mismatch (received=%s, expected=%s), ignoring...\n", mst, companyInfo.Mst);
      return;
    }
    
    if (strlen(idDevice) == 0 || strcmp(idDevice, TopicMqtt) != 0)
    {
      DEBUG_PRINTF("[MQTT] RequestLog: IdDevice mismatch (received=%s, expected=%s), ignoring...\n", idDevice, TopicMqtt);
      return;
    }
    
    // Validate BeginLog and Numslog
    if (beginLog < 1 || beginLog > MAX_LOGS)
    {
      DEBUG_PRINTF("[MQTT] RequestLog: Invalid BeginLog=%d (must be 1-%d)\n", beginLog, MAX_LOGS);
      setSystemStatus("ERROR", "RequestLog: Invalid BeginLog");
      return;
    }
    
    if (numsLog < 1 || numsLog > 200)
    {
      DEBUG_PRINTF("[MQTT] RequestLog: Invalid Numslog=%d (must be 1-200)\n", numsLog);
      setSystemStatus("ERROR", "RequestLog: Invalid Numslog");
      return;
    }
    
    DEBUG_PRINTF("[MQTT] RequestLog: MST=%s, IdDevice=%s, BeginLog=%d, Numslog=%d\n", 
                  mst, idDevice, beginLog, numsLog);
    
    // Build response topic: {Mst}/ResponseLog
    char responseTopic[64];
    snprintf(responseTopic, sizeof(responseTopic), "%s/ResponseLog", companyInfo.Mst);
    
    // Calculate range
    uint16_t endLog = beginLog + numsLog - 1;
    
    // Make sure we don't exceed MAX_LOGS
    if (endLog > MAX_LOGS)
    {
      endLog = MAX_LOGS;
      DEBUG_PRINTF("[MQTT] Adjusted endLog to MAX_LOGS=%d\n", MAX_LOGS);
    }
    
    DEBUG_PRINTF("[MQTT] Reading logs from %d to %d...\n", beginLog, endLog);
    
    // Create response JSON with compressed format (short keys + array values)
    DynamicJsonDocument responseDoc(32768); // 32KB buffer for up to 200 logs
    responseDoc["M"] = companyInfo.Mst;        // Mst
    responseDoc["I"] = TopicMqtt;              // IdDevice
    responseDoc["B"] = beginLog;                // BeginLog
    responseDoc["N"] = numsLog;                 // Numslog
    
    JsonArray logsArray = responseDoc.createNestedArray("L"); // Logs array
    
    int found = 0;
    int notFound = 0;
    
    // Read logs from Flash and add to array
    for (uint16_t logId = beginLog; logId <= endLog; logId++)
    {
      // Feed WDT every 20 logs to prevent timeout (20 logs × ~150ms = 3s per batch)
      if ((logId - beginLog) % 20 == 0)
      {
        esp_task_wdt_reset();
        yield(); // Allow other tasks to run
      }
      
      // Read log from Flash
      if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        File dataFile = LittleFS.open(FLASH_DATA_FILE, "r");
        if (dataFile)
        {
          PumpLog log;
          uint32_t offset = (logId - 1) * sizeof(PumpLog);
          dataFile.seek(offset, SeekSet);
          size_t bytesRead = dataFile.read((uint8_t*)&log, sizeof(PumpLog));
          dataFile.close();
          
          if (bytesRead == sizeof(PumpLog) && log.viTriLogData == logId)
          {
            // Add this log as compact array format: [id,voi,cot,data,bomb,lit,gia,total,tien,d,m,y,h,min,s,sent,time]
            JsonArray logArray = logsArray.createNestedArray();
            logArray.add(logId);                  // 0: logId
            logArray.add(log.idVoi);              // 1: idVoi
            logArray.add(log.viTriLogCot);        // 2: viTriLogCot
            logArray.add(log.viTriLogData);       // 3: viTriLogData
            logArray.add(log.maLanBom);           // 4: maLanBom
            logArray.add(log.soLitBom);           // 5: soLitBom
            logArray.add(log.donGia);             // 6: donGia
            logArray.add(log.soTotalTong);        // 7: soTotalTong
            logArray.add(log.soTienBom);          // 8: soTienBom
            logArray.add(log.ngay);               // 9: ngay
            logArray.add(log.thang);              // 10: thang
            logArray.add(log.nam);                // 11: nam
            logArray.add(log.gio);                // 12: gio
            logArray.add(log.phut);               // 13: phut
            logArray.add(log.giay);               // 14: giay
            logArray.add(log.mqttSent);           // 15: mqttSent
            
            // Format timestamp
            if (log.mqttSentTime > 0) {
              struct tm *timeinfo = localtime(&log.mqttSentTime);
              char formattedTime[32];
              snprintf(formattedTime, sizeof(formattedTime), "%02d/%02d/%04d-%02d:%02d:%02d",
                       timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
                       timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
              logArray.add(formattedTime);        // 16: mqttSentTime
            } else {
              logArray.add("N/A");                // 16: mqttSentTime
            }
            
            found++;
            DEBUG_PRINTF("[MQTT] ✓ Added log %d to response array\n", logId);
          }
          else
          {
            DEBUG_PRINTF("[MQTT] Log %d not found or invalid in Flash\n", logId);
            notFound++;
          }
        }
        else
        {
          DEBUG_PRINTLN("[MQTT] ✗ Failed to open Flash file for log read");
          notFound++;
        }
        xSemaphoreGive(flashMutex);
      }
      else
      {
        DEBUG_PRINTF("[MQTT] ⚠️ Flash mutex timeout for log %d\n", logId);
        notFound++;
      }
    }
    
    // Add summary to response (short keys)
    responseDoc["F"] = found;        // TotalFound
    responseDoc["X"] = notFound;     // TotalNotFound (X for eXcluded/miXing)
    
    // Serialize and publish the complete response
    String jsonString;
    serializeJson(responseDoc, jsonString);
    
    DEBUG_PRINTF("[MQTT] RequestLog Summary: Found=%d, NotFound=%d, Total=%d (from %d to %d)\n", 
                  found, notFound, numsLog, beginLog, endLog);
    DEBUG_PRINTF("[MQTT] Response JSON size: %d bytes\n", jsonString.length());
    
    if (found > 0)
    {
      if (mqttClient.publish(responseTopic, jsonString.c_str()))
      {
        DEBUG_PRINTF("[MQTT] ✓ Published %d logs to %s\n", found, responseTopic);
        char statusMsg[64];
        snprintf(statusMsg, sizeof(statusMsg), "Published %d log(s)", found);
        setSystemStatus("OK", statusMsg);
      }
      else
      {
        LOG_ERROR_F("[MQTT] ✗ Failed to publish response (size: %d bytes)\n", jsonString.length());
        setSystemStatus("ERROR", "Failed to publish log response - payload may be too large");
      }
    }
    else
    {
      DEBUG_PRINTLN("[MQTT] No logs found to publish");
      setSystemStatus("WARNING", "No logs found");
    }
  }
  
  Serial.println("=== MQTT CALLBACK FINISHED ===\n");
}

void sendMQTTData(const PumpLog &log)
{
  String jsonData = convertPumpLogToJson(log);

  int retryCount = 0;
  const int maxRetries = 3;
  bool mqttSuccess = false;
  esp_task_wdt_reset();

  while (retryCount < maxRetries)
  {
    if (mqttClient.publish(fullTopic, jsonData.c_str()))
    {
      Serial.println("MQTT data sent successfully");
      mqttSuccess = true;
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

  // Prepare updated log with MQTT status
  PumpLog updatedLog = log;
  updatedLog.mqttSentTime = time(NULL); // Timestamp from Google NTP (success or failure)
  
  if (mqttSuccess)
  {
    updatedLog.mqttSent = 1; // Success
    // Serial.printf("✓ Log %d: MQTT sent successfully at %ld\n", 
                  // updatedLog.viTriLogData, updatedLog.mqttSentTime);
  }
  else
  {
    updatedLog.mqttSent = 0; // Failed
    Serial.println("ERROR: MQTT send failed after maximum retries");
    Serial.printf("✗ Log %d: MQTT failed at %ld\n", 
                  updatedLog.viTriLogData, updatedLog.mqttSentTime);
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "MQTT send failed after %d retries", maxRetries);
    setSystemStatus("ERROR", errorMsg);
  }

  // Save to Flash at position viTriLogData (1-5000)
  // Skip if old partition detected (to prevent crashes)
  if (!g_flashSaveEnabled)
  {
    Serial.printf("⚠️ Flash save SKIPPED for Log %d (old partition detected)\n", updatedLog.viTriLogData);
  }
  else if (updatedLog.viTriLogData >= 1 && updatedLog.viTriLogData <= MAX_LOGS)
  {
    if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");
      if (!dataFile) {
        dataFile = LittleFS.open(FLASH_DATA_FILE, "w");
      }
      
      if (dataFile)
      {
        uint32_t offset = (updatedLog.viTriLogData - 1) * sizeof(PumpLog); // CRITICAL: uint32_t not uint16_t!
        dataFile.seek(offset, SeekSet);
        size_t written = dataFile.write((const uint8_t*)&updatedLog, sizeof(PumpLog));
        dataFile.close();
        
        if (written == sizeof(PumpLog)) {
          Serial.printf("💾 Log %d saved to Flash at offset %u (mqttSent=%d)\n", 
                        updatedLog.viTriLogData, offset, updatedLog.mqttSent);
        } else {
          Serial.printf("⚠️ Partial write for Log %d: %u/%u bytes\n", 
                        updatedLog.viTriLogData, written, sizeof(PumpLog));
        }
      }
      else
      {
        Serial.println("ERROR: Failed to open Flash file for log save");
      }
      xSemaphoreGive(flashMutex);
    }
    else
    {
      Serial.printf("⚠️ Flash mutex timeout for Log %d\n", updatedLog.viTriLogData);
      xSemaphoreGive(flashMutex);
    }
  }
  else
  {
    Serial.printf("ERROR: Invalid viTriLogData=%d\n", updatedLog.viTriLogData);
  }
}

void readRS485Data(byte *buffer)
{
  if (!Serial2.available())
  {
    return;
  }

  // RS485 Statistics for monitoring data quality
  static struct {
    unsigned long totalPackets = 0;
    unsigned long validLogs = 0;
    unsigned long invalidLogs = 0;
    unsigned long validPriceResponses = 0;
    unsigned long invalidPriceResponses = 0;
    unsigned long lastStatsReport = 0;
  } rs485Stats;
  
  // Report statistics every 10 minutes
  unsigned long now = millis();
  if (now - rs485Stats.lastStatsReport >= 600000) // 10 minutes
  {
    rs485Stats.lastStatsReport = now;
    if (rs485Stats.totalPackets > 0)
    {
      float validLogRate = (rs485Stats.validLogs * 100.0) / rs485Stats.totalPackets;
      float validPriceRate = (rs485Stats.validPriceResponses * 100.0) / rs485Stats.totalPackets;
      Serial.println("\n=== RS485 DATA QUALITY REPORT (10 min) ===");
      Serial.printf("Total packets: %lu\n", rs485Stats.totalPackets);
      Serial.printf("Valid logs: %lu (%.1f%%)\n", rs485Stats.validLogs, validLogRate);
      Serial.printf("Invalid logs: %lu\n", rs485Stats.invalidLogs);
      Serial.printf("Valid price responses: %lu (%.1f%%)\n", rs485Stats.validPriceResponses, validPriceRate);
      Serial.printf("Invalid price responses: %lu\n", rs485Stats.invalidPriceResponses);
      Serial.println("==========================================\n");
    }
  }
  
  // Limit processing to prevent overflow from TTL boot spam
  static unsigned long lastReadTime = 0;
  static int consecutiveReads = 0;
  
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
      // Validate header and footer FIRST to detect corrupt packets early
      if (priceResponse[0] != 7 || priceResponse[3] != 8)
      {
        DEBUG_PRINTF("[RS485 READ] ⚠️ Invalid price response header/footer: [0x%02X][0x%02X][0x%02X][0x%02X]\n",
                     priceResponse[0], priceResponse[1], priceResponse[2], priceResponse[3]);
        return;
      }
      
      uint8_t deviceId = priceResponse[1];
      char status = priceResponse[2];
      
      // Validate status byte BEFORE deviceId (catch corruption early)
      if (status != 'S' && status != 'E')
      {
        rs485Stats.totalPackets++;
        rs485Stats.invalidPriceResponses++;
        
        static int invalidStatusCount = 0;
        invalidStatusCount++;
        if (invalidStatusCount % 10 == 0)
        {
          Serial.printf("[RS485 READ] ⚠️ Invalid status byte: '%c' (0x%02X) - likely corrupt data (count: %d)\n", 
                        status, (uint8_t)status, invalidStatusCount);
        }
        return;
      }
      
      // Validate deviceId is in valid range (11-20)
      if (deviceId < 11 || deviceId > 20)
      {
        rs485Stats.totalPackets++;
        rs485Stats.invalidPriceResponses++;
        
        // Reduce log spam - only log every 10th invalid response
        static int invalidDeviceIdCount = 0;
        invalidDeviceIdCount++;
        if (invalidDeviceIdCount % 10 == 0)
        {
          Serial.printf("[RS485 READ] ⚠️ Ignoring invalid DeviceID=%d (count: %d, valid range: 11-20)\n", deviceId, invalidDeviceIdCount);
        }
        return;
      }
      
      // Valid price response
      rs485Stats.totalPackets++;
      rs485Stats.validPriceResponses++;
      
      // Log valid response (debug only)
      DEBUG_PRINTF("\n[RS485 READ] Price Change Response: ");
      for (int i = 0; i < 4; i++) {
        if (i == 2) {
          DEBUG_PRINTF("'%c'(0x%02X) ", priceResponse[i], priceResponse[i]);
        } else {
          DEBUG_PRINTF("0x%02X ", priceResponse[i]);
        }
      }
      DEBUG_PRINTLN("");
      
      // Parse status
      if (status == 'S')
      {
        DEBUG_PRINTF("[RS485 READ] ✓ SUCCESS - DeviceID=%d price updated successfully\n", deviceId);
        
        // Save price to Flash for this nozzle
        if (deviceId == lastPriceChangeRequest.deviceId)
        {
          // Convert deviceId to nozzorle string
          char nozzorleStr[4];
          snprintf(nozzorleStr, sizeof(nozzorleStr), "%d", deviceId);
          updateNozzlePrice(nozzorleStr, lastPriceChangeRequest.idDevice, 
                          lastPriceChangeRequest.unitPrice, nozzlePrices, flashMutex);
        }
        
        // Publish Complete response to MQTT
        if (mqttClient.connected() && deviceId == lastPriceChangeRequest.deviceId)
        {
          // Build response topic: {CompanyId}/FinishPrice
          char responseTopic[100];
          snprintf(responseTopic, sizeof(responseTopic), "%s/FinishPrice", companyInfo.CompanyId);
          
          // Build JSON response with new structure
          DynamicJsonDocument doc(1024);
          doc["topic"] = lastPriceChangeRequest.idChiNhanh;
          
          // Build clientid: {IDChiNhanh}/GetStatus/{TopicMqtt}
          char clientId[100];
          snprintf(clientId, sizeof(clientId), "%s/GetStatus/%s", 
                   lastPriceChangeRequest.idChiNhanh, TopicMqtt);
          doc["clientid"] = clientId;
          
          // Get timestamp from the nozzle that was just updated
          int nozzleIndex = deviceId - 11;  // Map 11-20 to index 0-9
          time_t updatedTimestamp = nozzlePrices.nozzles[nozzleIndex].updatedAt;
          
          // Format timestamp to dd/mm/yyyy-HH:MM:SS
          struct tm *timeinfo = localtime(&updatedTimestamp);
          char formattedTime[32];
          snprintf(formattedTime, sizeof(formattedTime), "%02d/%02d/%04d-%02d:%02d:%02d",
                   timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
                   timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
          
          doc["timestamp"] = formattedTime;
          
          // Build message array
          JsonArray messageArray = doc.createNestedArray("message");
          JsonObject entry = messageArray.createNestedObject();
          entry["Key"] = "UpdatePrice";
          
          JsonObject item = entry.createNestedObject("item");
          item["IDChiNhanh"] = lastPriceChangeRequest.idChiNhanh;
          item["IdDevice"] = lastPriceChangeRequest.idDevice;
          item["UnitPrice"] = lastPriceChangeRequest.unitPrice;
          item["UpdatedAt"] = formattedTime;  // Formatted timestamp từ Flash
          
          String jsonString;
          serializeJson(doc, jsonString);
          
          if (mqttClient.publish(responseTopic, jsonString.c_str()))
          {
            Serial.printf("[RS485 READ] ✓ Published FinishPrice to %s: %s\n", responseTopic, jsonString.c_str());
          }
          else
          {
            Serial.printf("[RS485 READ] ✗ Failed to publish FinishPrice to %s\n", responseTopic);
          }
        }
      }
      else if (status == 'E')
      {
        Serial.printf("[RS485 READ] ✗ ERROR - DeviceID=%d rejected price update (KPL returned 'E')\n", deviceId);
        Serial.printf("[RS485 READ] ⚠️ Error response ignored - NOT publishing to MQTT\n");
        // Do NOT publish Error response to MQTT - only Success is published
      }
      else
      {
        Serial.printf("[RS485 READ] ✗ UNKNOWN STATUS - DeviceID=%d, Status='%c' (0x%02X)\n", 
                      deviceId, status, status);
        Serial.printf("[RS485 READ] ⚠️ Unknown status ignored - NOT publishing to MQTT\n");
      }
    }
    else
    {
      Serial.printf("[RS485 READ] Invalid price response format: [0x%02X][0x%02X][0x%02X][0x%02X]\n",
                    priceResponse[0], priceResponse[1], priceResponse[2], priceResponse[3]);
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
        // Valid pump log
        rs485Stats.totalPackets++;
        rs485Stats.validLogs++;

        // Process valid pump log data
    PumpLog log;
      ganLog(buffer, log);
        
      // kiểm tra mqttQueue có dữ liệu không
      // if (uxQueueMessagesWaiting(mqttQueue) == 0)
      // {
      //   Serial.println("MQTT queue is empty, skipping...");
      //   return;
      // }

      if (xQueueSend(mqttQueue, &log, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        Serial.println("Log data queued for MQTT");
        // Trigger relay
          xTaskCreate(ConnectedKPLBox, "ConnectedKPLBox", 1024, NULL, 4, NULL);
        }
      }
      else
      {
        // Invalid pump log
        rs485Stats.totalPackets++;
        rs485Stats.invalidLogs++;
        
        // Enhanced error logging for debugging corruption
        static int invalidLogCount = 0;
        invalidLogCount++;
        
        // Log every 10th error to avoid spam, but provide detailed info
        if (invalidLogCount % 10 == 0)
        {
          Serial.printf("[RS485 READ] ❌ Invalid pump log #%d:\n", invalidLogCount);
          Serial.printf("  Checksum: calc=0x%02X recv=0x%02X %s\n", 
                        calculatedChecksum, buffer[30], 
                        (calculatedChecksum == buffer[30] ? "✓" : "✗"));
          Serial.printf("  Header: [0x%02X][0x%02X] %s\n", 
                        buffer[0], buffer[1],
                        (buffer[0] == 1 && buffer[1] == 2 ? "✓" : "✗"));
          Serial.printf("  Footer: [0x%02X][0x%02X] %s\n", 
                        buffer[29], buffer[31],
                        (buffer[29] == 3 && buffer[31] == 4 ? "✓" : "✗"));
          
          // Dump first 8 bytes for pattern analysis
          Serial.print("  Data preview: ");
          for(int i = 0; i < 8; i++) {
            Serial.printf("0x%02X ", buffer[i]);
          }
          Serial.println();
        }
        else
        {
          // Reduced logging for other errors
          DEBUG_PRINTLN("[RS485 READ] Invalid pump log: checksum or format error");
        }
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
    // Reduce spam by only logging every 10th invalid byte
    static int invalidByteCount = 0;
    invalidByteCount++;
    
    if (invalidByteCount % 10 == 0)
    {
      Serial.printf("[RS485 READ] Unknown first byte: 0x%02X (count: %d), discarding...\n", firstByte, invalidByteCount);
    }
    
    Serial2.read(); // Discard invalid byte
    
    // Reset counter every 100 invalid bytes to prevent overflow
    if (invalidByteCount >= 100)
    {
      invalidByteCount = 0;
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

void sendPriceChangeCommand(const PriceChangeRequest &request)
{
  // Save request for response publishing
  lastPriceChangeRequest = request;
  
  Serial.printf("\n[PRICE CHANGE] Sending command: DeviceID=%d, IdDevice=%s, IDChiNhanh=%s, Price=%.2f\n", 
                request.deviceId, request.idDevice, request.idChiNhanh, request.unitPrice);
  
  // Clear RX buffer before sending
  while (Serial2.available()) {
    Serial2.read();
  }
  
  // Format price to 6 digits ASCII
  char priceStr[7];
  snprintf(priceStr, sizeof(priceStr), "%06d", static_cast<int>(request.unitPrice));
  
  // Prepare command buffer
  // Protocol: [PM(9)] [ID_Device] [Price_Char0-5 (6 bytes)] [Checksum] [LF(10)]
  byte command[10];
  command[0] = 9;
  command[1] = static_cast<byte>(request.deviceId);
  
  // Price digits (reversed order as per protocol)
  command[2] = priceStr[5];  // Char(0)
  command[3] = priceStr[4];  // Char(1)
  command[4] = priceStr[3];  // Char(2)
  command[5] = priceStr[2];  // Char(3)
  command[6] = priceStr[1];  // Char(4)
  command[7] = priceStr[0];  // Char(5)
  
  // Calculate checksum: 0x5A XOR all data bytes (ID + Price chars)
  uint8_t checksum = 0x5A ^ command[1] ^ command[2] ^ command[3] ^ command[4] ^ command[5] ^ command[6] ^ command[7];
  command[8] = checksum;
  command[9] = 10;  // End byte (LF)
  
  // Send command
  Serial2.write(command, 10);
  
  Serial.print("[PRICE CHANGE] Sent: ");
  for (int i = 0; i < 10; i++) {
    Serial.printf("0x%02X ", command[i]);
  }
  Serial.println();
}

void ConnectedKPLBox(void *param)
{
  digitalWrite(OUT1, HIGH);
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
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "OTA: API Error %d", httpCode);
    setSystemStatus("ERROR", errorMsg);
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
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "OTA: %s", Update.errorString());
    setSystemStatus("ERROR", errorMsg);
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
    char errorMsg[64];
    snprintf(errorMsg, sizeof(errorMsg), "OTA: %s", Update.errorString());
    setSystemStatus("ERROR", errorMsg);
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

// CRITICAL: New function to print partition table info
void printPartitionInfo()
{
  Serial.println("\n--- VERIFYING PARTITION TABLE ---");
  const esp_partition_t *partition = NULL;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  if (it) {
    Serial.println("App Partitions:");
    do {
      partition = esp_partition_get(it);
      if (partition) {
        Serial.printf("  - Name: %s, Type: %d, Subtype: %d, Address: 0x%X, Size: %u (%.2f MB)\n",
                      partition->label, partition->type, partition->subtype,
                      partition->address, partition->size, partition->size / 1024.0 / 1024.0);
      }
    } while ((it = esp_partition_next(it)));
    esp_partition_iterator_release(it);
  }

  // Check for littlefs partition
  bool hasCorrectPartition = false;
  it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
  if (it) {
    Serial.println("Data Partitions:");
    do {
      partition = esp_partition_get(it);
      if (partition) {
        Serial.printf("  - Name: %s, Type: %d, Subtype: %d, Address: 0x%X, Size: %u (%.2f KB)\n",
                      partition->label, partition->type, partition->subtype,
                      partition->address, partition->size, partition->size / 1024.0);
        
        if (strcmp(partition->label, "littlefs") == 0) {
          if (partition->size == 0xA0000) {
            Serial.println("    [✓] CORRECT: LittleFS size is 640KB. OK.");
            hasCorrectPartition = true;
          } else if (partition->size == 0x200000) {
            Serial.println("    [❌] ERROR: OLD PARTITION! LittleFS size is 2MB. THIS IS THE PROBLEM!");
          } else {
            Serial.printf("    [⚠️] WARNING: UNEXPECTED LittleFS size: %u bytes.\n", partition->size);
          }
        }
      }
    } while ((it = esp_partition_next(it)));
    esp_partition_iterator_release(it);
  }
  
  // If no littlefs or wrong size, show update instructions
  if (!hasCorrectPartition) {
    Serial.println("\n╔═════════════════════════════════════════════════════════╗");
    Serial.println("║  ⚠️  THIẾT BỊ CẦN CẬP NHẬT PARTITION TABLE ⚠️         ║");
    Serial.println("╚═════════════════════════════════════════════════════════╝");
    Serial.println("\nThiết bị đang dùng partition cũ/sai, cần flash lại USB.");
    Serial.println("\nHƯỚNG DẪN CẬP NHẬT:");
    Serial.println("1. Tải công cụ: https://kpltech.vn/flash-tool");
    Serial.println("2. Kết nối thiết bị qua USB");
    Serial.println("3. Chạy: flash-update.bat (Windows) hoặc flash-update.sh (Mac/Linux)");
    Serial.println("4. Đợi 5 phút để hoàn tất");
    Serial.println("\n⚠️ Thiết bị vẫn hoạt động NHƯNG KHÔNG LƯU LOG vào Flash");
    Serial.println("để tránh crash. Logs vẫn được gửi MQTT bình thường.");
    Serial.println("\nHỗ trợ kỹ thuật: 0xxx-xxx-xxx (miễn phí)");
    Serial.println("═════════════════════════════════════════════════════════\n");
    
    // Disable flash writes to prevent crashes
    g_flashSaveEnabled = false;
    
    // Send alert to MQTT if connected
    if (WiFi.status() == WL_CONNECTED) {
      // Will send alert after MQTT connects in main loop
    }
  } else {
    g_flashSaveEnabled = true;
  }
  
  Serial.println("-----------------------------------\n");
}

// SECURITY: Validate MAC address with server
bool validateMacWithServer(const char* macAddress)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ No WiFi - skipping MAC validation (will retry after WiFi connects)");
    return true; // Allow boot without WiFi, will validate later
  }
  
  HTTPClient http;
  String url = String(API_BASE_URL) + "/validate-mac"; // New endpoint
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000); // 10 second timeout
  
  // Create validation request
  DynamicJsonDocument doc(256);
  doc["mac"] = macAddress;
  doc["deviceId"] = TopicMqtt;
  doc["firmwareVersion"] = "2024.11.04";
  doc["timestamp"] = time(NULL);
  
  String json;
  serializeJson(doc, json);
  
  Serial.printf("Validating MAC with server: %s\n", macAddress);
  
  int httpCode = http.POST(json);
  String response = http.getString();
  
  if (httpCode == 200) {
    // Parse response
    DynamicJsonDocument responseDoc(512);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (!error && responseDoc["authorized"].as<bool>()) {
      Serial.println("✓ MAC validated successfully");
      return true;
    } else {
      Serial.printf("❌ Server rejected MAC: %s\n", responseDoc["message"].as<const char*>());
      return false;
    }
  } else if (httpCode == 403) {
    Serial.println("❌ MAC not authorized by server");
    return false;
  } else {
    Serial.printf("⚠️ Server validation failed (HTTP %d) - allowing boot\n", httpCode);
    return true; // Allow boot if server is down (graceful degradation)
  }
  
  http.end();
}