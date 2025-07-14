#include <Wire.h>
// #include <ETH.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include "Settings.h"
#include <esp_task_wdt.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <vector>
#include <math.h> // Thư viện math.h để sử dụng hàm floor(
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <structdata.h>
#include <ESP32Ping.h>
#include <Setup.h>
#include <FlashFile.h>
#include <Inits.h>
#include <Api.h>
#include <TTL.h>
#include <WebServer.h>
#include "Webservice.h"
#include <ESPAsyncWebServer.h>
// Khởi tạo client

// Cấu hình Ethernet
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // Địa chỉ MAC duy nhất
WiFiClient ethClient;                              // Ethernet client
PubSubClient client(ethClient);                    // MQTT client sử dụng Ethernet client
// Biến trạng thái cho kết nối và các tác vụ
// bool ethConnected = false;
unsigned long counterReset = 0;
bool ethPreviouslyConnected = false; // Cờ kiểm tra trạng thái kết nối trước đó

// Khai báo dữ liệu struct data
DeviceStatus *deviceStatus = new DeviceStatus;
CompanyInfo *companyInfo = new CompanyInfo; // lấy thông tin của company khi internet được kết nối
Settings *settings = new Settings;
TimeSetup *timeSetup = new TimeSetup;
GetIdLogLoss *receivedMessage = new GetIdLogLoss;

uint32_t currentId = 0;                                        // ID vô hạn
const int numOfVoi = sizeof(idVoiList) / sizeof(idVoiList[0]); // Số lượng vòi

// Thông tin chung của topic mqtt connected
char fullTopic[50]; // Đảm bảo kích thước đủ lớn
char topicStatus[50];
char topicError[50];
char topicRestart[50];
char topicGetLogIdLoss[50];
char topicShift[50];
char topicChange[50];
char shift[30];

// Khai báo queue để lưu trữ dữ liệu trước khi gửi qua MQTT
QueueHandle_t mqttQueue;
QueueHandle_t logIdLossQueue;

// Khai báo Mutex
SemaphoreHandle_t flashMutex;

// Khai báo handler task
TaskHandle_t Handle_Rs485 = NULL;
TaskHandle_t Handle_MqttSend = NULL;
TaskHandle_t getIdLogLossHandle = NULL;
// TaskHandle_t ethernetTaskhandle = NULL;
TaskHandle_t Handle_Wifi = NULL;

// Function prototypes
void formatLittleFS();
void runCommandRs485(void *parameter);
void parseAndConvertToJson(String input);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void getIdLogLoss(void *param);
// void ethernetTask(void *pvParameters);
void getInfoConnectMqtt();
void checkInternetConnection();
void sendDataCommand(String command, char *&param);
void processAllVoi(TimeSetup *time); // Xử lý tất cả các ID vòi
void mqttSendTask(void *parameter);
void checkHeap();
void readRs485(uint8_t *);
void ConnectWifiMqtt(void *parameter);
eTaskState checkTaskState(TaskHandle_t taskHandle);
void webServerTask(void *parameter);
void ConnectedKPLBox(void * param);
// Bổ xung hàm scanwwifi và kết nối wifi theo kiểu bất đồng bộ
void scanWiFi();
void connectWiFi(AsyncWebServerRequest *request);

bool statusConnected = false;

// Khởi tạo WebServer
// WebServer server(80);
// Khoi tạo webserver bất đồng bộ
AsyncWebServer server(80);
// Biến để kiểm tra trạng thái đăng nhập
bool isLoggedIn = false;

const char *apSSID = "KPL-Qa-Gas-Device";
String wifiList = "";

void setupTime()
{
  // Cấu hình thời gian
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  int retry = 0;
  const int maxRetries = 5;

  // Thử lấy thời gian, nếu thất bại thì gọi lại configTime và thử lại
  while (!getLocalTime(&timeinfo) && retry < maxRetries)
  {
    Serial.println("Failed to obtain time. Retrying...");
    delay(2000); // Chờ 2 giây
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    retry++;
  }

  

  if (retry == maxRetries)
  {
    Serial.println("Failed to obtain time after multiple retries");
    // Nếu muốn khởi động lại thì bỏ comment dòng sau:
    ESP.restart();
  }
  else
  {
    setUpTime(timeSetup, timeinfo);
    // Đồng bộ thời gian với bên KPL
    processAllVoi(timeSetup); // Xử lý tất cả các ID vòi
    Serial.println("Time acquired successfully:");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  }
}


/// @brief Hàm setup thiết lập các thông tin ban đầu
void setup()
{
  Serial.begin(115200);
  pinMode(OUT1, OUTPUT);
  pinMode(OUT2, OUTPUT);
  esp_task_wdt_init(60, true);
  esp_task_wdt_add(NULL); // Thêm các nhiệm vụ bạn muốn giám sát, NULL để giám sát tất cả nhiệm vụ
  // Initialize RS485
  Serial2.begin(RS485BaudRate, SERIAL_8N1, RX_PIN, TX_PIN);
  // Khởi động Wifi
  WiFi.mode(WIFI_STA);

  // Cấu hình MQTT server (thay thế bằng địa chỉ MQTT Broker của bạn)
  client.setServer(mqttServer, 1883);
  client.setCallback(mqttCallback);

  // Khởi tạo Queue ban đầu
  mqttQueue = xQueueCreate(15, sizeof(PumpLog));
  logIdLossQueue = xQueueCreate(100, sizeof(DtaLogLoss));

  // Create mutex for flash access
  flashMutex = xSemaphoreCreateMutex();
  // Initialize flash (SPIFFS)
  initLittleFS();
  readFlashSettings(flashMutex, deviceStatus, counterReset);
  // Tính toán currentId từ kích thước file log
  currentId = initializeCurrentId(flashMutex);
  Serial.printf("Log read successfully for ID: %u\n", currentId);
  Serial.println("Setup finished");

  xTaskCreate(webServerTask, "webServerTask", 16384, NULL, 1, NULL);
  delay(3000);

   // Thông báo KPL ESP32 đã sẵn sàng tiến hành gởi lệnh xuống KPL để đọc dữ liệu
  sendStartupCommand();
  delay(3000);
   // Thông báo KPL ESP32 đã sẵn sàng tiến hành gởi lệnh xuống KPL để đọc dữ liệu
  sendStartupCommand();

  // xTaskCreate(ethernetTask, "ethernetTask", 8192, NULL, 1, &ethernetTaskhandle);
  xTaskCreate(runCommandRs485, "runCommandRs485", 16384, NULL, 2, &Handle_Rs485);

  xTaskCreate(ConnectWifiMqtt, "connectWifiMqtt", 8192, NULL, 1, &Handle_Wifi);
  // Cấu hình NTP
  while (statusConnected==false)
  {
    vTaskDelay(100/ portTICK_PERIOD_MS);
    esp_task_wdt_reset();
    Serial.print(".");
  }
  Serial.println("Thiet lap thoi gian:");

  setupTime();
  
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // // In ra thời gian ban đầu
  // struct tm timeinfo;
  // if (!getLocalTime(&timeinfo))
  // {
  //   Serial.println("Failed to obtain time");
  //   // ESP.restart();
  // } 
  // Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  // setUpTime(timeSetup, timeinfo);
  // // Đồng bộ thời gian với bên KPL
  // processAllVoi(timeSetup); // Xử lý tất cả các ID vòi
  // // Thông báo KPL ESP32 đã sẵn sàng
  // sendStartupCommand();

  // // xTaskCreate(ethernetTask, "ethernetTask", 8192, NULL, 1, &ethernetTaskhandle);
  // xTaskCreate(runCommandRs485, "runCommandRs485", 16384, NULL, 2, &Handle_Rs485);
  xTaskCreate(mqttSendTask, "mqttSendTask", 8192, NULL, 3, &Handle_MqttSend);
  
}

unsigned long timer = 0;
void loop()
{
  // check heap
  checkHeap();

  // Kiểm tra thông tin của thiết bị cơ bản: Head memory
  if (client.connected())
  {
    client.loop();
    if (statusConnected == true)
    {
      // Kích hoạt lại các task cần thiết
      if (checkTaskState(Handle_MqttSend) == eSuspended)
      {
        vTaskResume(Handle_MqttSend);
      }
      vTaskSuspend(Handle_Wifi);
      statusConnected = false;
    }
  }
  else
  {
    Serial.println("Loss connect wifi");
    if (checkTaskState(Handle_MqttSend) != eSuspended)
    {
      vTaskSuspend(Handle_MqttSend);
    }
    // Kích hoạt lại task ConnectWifiMqtt
    if (checkTaskState(Handle_Wifi) == eSuspended && statusConnected == false)
    {
      vTaskResume(Handle_Wifi);
    }
  }

  esp_task_wdt_reset();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  yield();
}

eTaskState checkTaskState(TaskHandle_t taskHandle){
  eTaskState state = eTaskGetState(taskHandle);
  return state;
}

// Hàm quét WiFi và trả về HTML
// Quét danh sách WiFi
void scanWiFi() {
  int networks = WiFi.scanNetworks();
  wifiList = "[";
  for (int i = 0; i < networks; i++) {
      if (i > 0) wifiList += ",";
      wifiList += "\"" + WiFi.SSID(i) + "\"";
  }
  wifiList += "]";
  WiFi.scanDelete();  // Giải phóng bộ nhớ sau khi quét
}


// Xử lý khi người dùng gửi yêu cầu kết nối WiFi
void connectWiFi(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();

      request->send(200, "text/html", "<h3>Đang kết nối tới " + ssid + "...</h3> <a href='/'>Quay lại</a>");
      
      Serial.println("Kết nối tới WiFi: " + ssid);
      WiFi.begin(ssid.c_str(), password.c_str());

      int timeout = 15; // 15 giây timeout
      while (WiFi.status() != WL_CONNECTED && timeout > 0) {
          delay(1000);
          Serial.print(".");
          timeout--;
      }

      if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nKết nối thành công! IP: " + WiFi.localIP().toString());
      } else {
          Serial.println("\nKết nối thất bại!");
      }
  } else {
      request->send(400, "text/plain", "Thiếu SSID hoặc mật khẩu!");
  }
}

// Hàm kiểm tra kết nối internet bằng cách ping đến DNS Google
bool isConnectedToInternet() {
  return ethClient.connect("8.8.8.8", 53);  // Kiểm tra kết nối đến Google DNS
}

// Xử lý route "/check"
void handleCheckWiFi(AsyncWebServerRequest *request) {
  if (isConnectedToInternet()) {
      request->send(200, "text/html", checkInternet);
  } else {
    request->send(200, "text/html", checkInternetNoConnect);
    vTaskDelay(2000/ portTICK_PERIOD_MS);
    request->redirect("/config");
  }
}


// Task chạy webserver
// Thực hiện theo phương thức bất đồng bộ
void webServerTask(void *parameter) {

  WiFi.softAP(apSSID, "Qu0c@nh1");
  Serial.println("WiFi AP Started: " + String(apSSID));

  scanWiFi();

  // Cấu hình Web Server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!isLoggedIn) {
          request->redirect("/login");
      } else {
          request->redirect("/config");
      }
  });

  // Vao trang Login
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", loginPage);
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
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
  });

  server.on("/logout", HTTP_GET, [](AsyncWebServerRequest *request) {
      isLoggedIn = false;
      request->redirect("/login");
  });

  server.on("/check", HTTP_GET, handleCheckWiFi);  // Route kiểm tra trạng thái Wi-Fi

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!isLoggedIn) {
          request->redirect("/login");
      } else {
          request->send(200, "text/html", configPage1);
      }
  });

  server.on("/wifi_scan", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "application/json", wifiList);
  });

  // Xử lý lưu WiFi (có thể bổ sung)
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!isLoggedIn) {
          request->redirect("/login");
          return;
      }
      if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
          String ssid = request->getParam("ssid", true)->value();
          String password = request->getParam("password", true)->value();
          String id = request->getParam("id", true)->value();
          Serial.println("WiFi lưu: " + ssid + " - " + password + " - " + id);
          String data = ssid + "\n" + password + "\n" + id;
          writeFileConfig("/config.txt", data);
          request->send(200, "text/html", result);
          delay(2000);
          ESP.restart();
      }else {
          request->send(200, "text/html", serverFail);
        }
  });

  server.begin();
  Serial.println("Web server đã sẵn sàng!");

  vTaskDelete(NULL);
}

/// @brief hàm kết nối Wifi và Mqtt
/// @param parameter
void ConnectWifiMqtt(void *parameter){
  int retryConenct = 0;

  String configData = readFileConfig("/config.txt");
  if (configData != "") {
      // Không có dữ liệu cấu hình, bật chế độ AP
      // Có dữ liệu cấu hình, kết nối Wi-Fi
      int split1 = configData.indexOf('\n');
      int split2 = configData.lastIndexOf('\n');

      String ssid = configData.substring(0, split1);
      String password = configData.substring(split1 + 1, split2);
      String topic = configData.substring(split2 + 1);
      Serial.printf("wifi: %s, Pass: %s, TopicMqtt: %s", ssid, password, topic);
      // Ghi đè SSID và Password mặc định
      strncpy(wifi_ssid, ssid.c_str(), sizeof(wifi_ssid) - 1);
      strncpy(wifi_password, password.c_str(), sizeof(wifi_password) - 1);
      strncpy(TopicMqtt, topic.c_str(), sizeof(TopicMqtt) - 1);

      wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';   // Đảm bảo chuỗi kết thúc
      wifi_password[sizeof(wifi_password) - 1] = '\0'; // Đảm bảo chuỗi kết thúc
      TopicMqtt[sizeof(TopicMqtt) - 1] = '\0'; // Đảm bảo chuỗi kết thúc
  }
  
  while (true)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Connecting to WiFi... " + retryConenct);
      WiFi.begin(wifi_ssid, wifi_password);
      int retryCount = 0;
      while (WiFi.status() != WL_CONNECTED && retryCount < 30)
      {
        esp_task_wdt_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        Serial.print(".");
        retryCount++;
      }
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("\nWiFi connected");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        Serial.println("get data from server Settings MQTT");
        callAPIGetSettingsMqtt(settings, flashMutex);
        Serial.println(settings->MqttServer);
        Serial.println(settings->PortMqtt);
        client.setServer(settings->MqttServer, settings->PortMqtt);
        delay(1000);

        Serial.println("get data from server Company information");
        xTaskCreate(callAPIServerGetCompanyInfo, "getCompanyInfo", 2048, companyInfo, 2, NULL);
        listFiles(flashMutex);
        delay(4000);
        // Cập nhật thông tin của các topic
        strcpy(fullTopic, companyInfo->Mst);
        strcpy(topicStatus, companyInfo->Mst);
        strcpy(topicError, companyInfo->Mst);
        strcpy(topicRestart, companyInfo->Mst);
        strcpy(topicGetLogIdLoss, companyInfo->Mst);
        strcpy(topicShift, companyInfo->Mst);
        strcpy(topicChange, companyInfo->Mst);

        strcat(fullTopic, TopicSendData);
        strcat(topicStatus, TopicStatus);
        strcat(topicError, TopicLogError);
        strcat(topicRestart, TopicRestart);
        strcat(topicGetLogIdLoss, TopicGetLogIdLoss);
        // strcat(topicGetLogIdLoss, TopicGetLogIdLoss);
        strcat(topicShift, TopicShift);
        strcat(topicChange, TopicChange);

        // Nối chuỗi thứ hai vào mảng
        strcat(fullTopic, TopicMqtt);
        strcat(topicStatus, TopicMqtt);
        // strcat(topicGetLogIdLoss, TopicMqtt);
        Serial.println(fullTopic);
        Serial.println(topicGetLogIdLoss);
        Serial.println(companyInfo->CompanyId);
      }
      else
      {
        Serial.println("\nWiFi connection failed, retrying...");
        retryConenct++;
        if (retryConenct > 20)
          ESP.restart();
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi trước khi thử lại
        continue;
      }
    }
    // Kết nối MQTT
    if (!client.connected())
    {
      // get data from server
      esp_task_wdt_reset();
      Serial.println("Connecting to MQTT...");
      if (client.connect(TopicMqtt, mqttUser, mqttPassword))
      {
        Serial.println("MQTT connected");
        client.subscribe(topicError);
        client.subscribe(topicRestart);
        client.subscribe(topicGetLogIdLoss);
        client.subscribe(topicChange);
        client.subscribe(topicShift);
        delay(1000);
        statusConnected = true;
        // Thực hiện việc gọi API để lấy dữ liệu có Id bị loss
        // Đăng ký các topic
      }
      else
      {
        Serial.println("MQTT connection failed, retrying...");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi trước khi thử lại
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Chờ trước khi kiểm tra lại kết nối
  }
  esp_task_wdt_reset();
  vTaskDelete(NULL);
}

void mqttSendTask(void *parameter)
{
  TickType_t getTick;
  getTick = xTaskGetTickCount();
  String data = ""; // Dữ liệu lấy từ hàng đợi
  PumpLog log;
  Serial.println("Started task mqtt send");
  // Biến đếm thời gian
  int elapsedSeconds = 0;
  const int interval = 300; // 300 giây = 5 phút
    // Biến đánh dấu dữ liệu được gửi lên
  bool dataReceived = false;
  while (true)
  {
    // Serial.print("Vao day nha");
    // Nhận dữ liệu từ hàng đợi
    if (!client.connected())
    {
      /* code */
      Serial.println("Not Connected Internet/ Mqtt");

    }else if (xQueueReceive(mqttQueue, &log, pdMS_TO_TICKS(500)))
    {
      String jsondata = convertPumpLogToJson(log);
      Serial.printf("LogCotBom: %d  -- LogData: %d\n", log.viTriLogCot, log.viTriLogData);

      int retryCount = 0;         // Số lần thử lại
      const int maxRetries = 3;  // Giới hạn số lần thử lại
      elapsedSeconds = 0;
      dataReceived = true; // Dữ liệu được gửi lên
      while (retryCount < maxRetries)
      {
        // Thử gửi dữ liệu qua MQTT
        if (client.publish(fullTopic, jsondata.c_str()))
        {
          Serial.printf("MQTT sent successfully\n");
          break; // Thoát vòng lặp retry khi gửi thành công
        }
        else
        {
          Serial.printf("MQTT send failed (Attempt %d/%d)\n", retryCount + 1, maxRetries);
          retryCount++;
          vTaskDelay(500 / portTICK_PERIOD_MS); // Đợi trước khi thử lại
        }
        
      }

      // Nếu vượt quá số lần thử lại, xử lý lỗi
      if (retryCount == maxRetries)
      {
        Serial.println("Error: MQTT send failed after maximum retries. Discarding data.");
      }
      vTaskDelay(100/portTICK_PERIOD_MS);
    }else {
        // Không có dữ liệu, tăng thời gian đếm
        elapsedSeconds += 1; // Tăng 1 giây cho mỗi chu kỳ

        // Nếu đủ 5 phút và không có dữ liệu được gửi lên
        if (elapsedSeconds >= interval)
        {
          if (!dataReceived)
          {
            Serial.println("Không có dữ liệu trong 5 phút, gửi lệnh xuống...");
            sendStartupCommand();
          }

          // Reset biến đánh dấu và bộ đếm thời gian
          dataReceived = false;
          elapsedSeconds = 0;
        }
      }

    // Chờ trước khi tiếp tục vòng lặp
    vTaskDelayUntil(&getTick, 100 / portTICK_PERIOD_MS);
  }
}

/// @brief Hàm này đọc thông tin phản hồi về từ mqtt publicer
/// @param topic
/// @param payload
/// @param length
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  if (strcmp(topic, topicRestart) == 0)
  {
    // ESP.restart();
    Serial.print("Restart\n");
  }
  if (strcmp(topic, topicError) == 0)
  {
    parsePayload_IdLogLoss(payload, length, receivedMessage, companyInfo);
    // Serial.println(receivedMessage->Idvoi);
    Serial.printf("Idvoi: %s  Mst: %s \n", receivedMessage->Idvoi, receivedMessage->CompanyId);

    if (logIdLossQueue != NULL)
    {
      if (strcmp(receivedMessage->Idvoi, TopicMqtt) == 0)
      {
        Serial.println("Lay lai thong tin giao dich");
        Serial.printf("NumsLog: %d\n", uxQueueMessagesWaiting(logIdLossQueue));

        if (uxQueueMessagesWaiting(logIdLossQueue) == 0)
        {
          TaskParams *taskParams = new TaskParams;
          taskParams->msg = new GetIdLogLoss(*receivedMessage); // Sao chép dữ liệu
          taskParams->logIdLossQueue = logIdLossQueue;
          // Tạo task
          if (xTaskCreate(callAPIServerGetLogLoss, "GetData", 40960, taskParams, 3, NULL) != pdPASS)
          {
            Serial.println("Failed to create task");
            delete taskParams->msg;
            delete taskParams;
          }
        }
      }
    }
    else
    {
      Serial.println("Received message or queue is NULL.");
    }
  }
}

/// @brief Hàm đọc và xử lý tín hiệu
/// @param param
void runCommandRs485(void *param)
{
  TickType_t getTick;
  getTick = xTaskGetTickCount();
  byte buffer[LOG_SIZE];
  unsigned long count = 0;
  while (true)
  {
    count++;
    esp_task_wdt_reset();
    if (count % 2 == 0)
    {
      QueueHandle_t b = logIdLossQueue;
      if (uxQueueMessagesWaiting(b) > 0)
      {
        Serial.print("NumLogsLoss: ");
        Serial.println(uxQueueMessagesWaiting(b));
        DtaLogLoss dataLogId;
        if (xQueueReceive(logIdLossQueue, &dataLogId, 0))
        {
          Serial.printf("Da goi lenh xuong KPL: %d\n", dataLogId.Logid);
          sendLogRequest(dataLogId.Logid);
          vTaskDelay(100/ portTICK_PERIOD_MS);
          readRs485(buffer);
        }
      }
    }
    else
    {
      // Serial.println("Vao doc RS485");
      readRs485(buffer);
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
    // continue;
  }
}

void readRs485(byte * buffer)
{
  if (!Serial2 || !Serial2.availableForWrite())
  {
    Serial.println("Cổng Rs485 Chưa sẳn sàng\n");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    // continue;
  }
  static String receivedString = ""; // Biến lưu chuỗi nhận được
  static int counter = 0;            // Đếm số chuỗi đã xử lý
  // Gọi hàm nhận dữ liệu
  if (Serial2.available()>0)
  {
    // Đọc 32 byte
    Serial2.readBytes(buffer, LOG_SIZE);
    // Kiểm tra checksum
    uint8_t calculatedChecksum = calculateChecksum_LogData(buffer, LOG_SIZE);
    // Serial.print("checkSumcalculatedChecksum = " + String(calculatedChecksum)+"\n");
    PumpLog log;
    log.checksum = buffer[30];
    // Serial.print("CheckSum[30] = " + String(log.checksum)+"\n");
    // Kiểm tra điều kiện log gởi lên có đúng hay ko
    if (calculatedChecksum == log.checksum && buffer[0] == 1 && buffer[1] == 2 && buffer[29] == 3 && buffer[31] == 4)
    {
      // Gán dữ liệu vào struct
      ganLog(buffer, log);
      // String jsondata = convertPumpLogToJson(log);
      // Serial.printf("Len: %d\n", jsondata.length());

      if (xQueueSend(mqttQueue, &log, pdMS_TO_TICKS(300)) != pdPASS)
      {
        Serial.println("Error: Failed to send JSON pointer to MQTT queue");
      }
      else
      {
        // Serial.println("JSON sent to queue successfully");
        xTaskCreate(ConnectedKPLBox, "ConnectedKPLBox", 1023, NULL, 3, NULL);
        // Serial.println(jsondata);
      }
    }
  }
}



/// @brief Task dùng để kiểm tra việc kết nối internet
/// @param pvParameters
// void ethernetTask(void *pvParameters)
// {
//   while (true)
//   {
//     checkInternetConnection();
//     if (ETH.linkUp() && ethConnected && !ethPreviouslyConnected)
//     {
//       getInfoConnectMqtt();
//       ethPreviouslyConnected = true; // Cập nhật trạng thái đã kết nối
//       Serial.println("Ethernet connected on interneted");

//       Serial.println("Connecting to MQTT...");
//       if (client.connect(TopicMqtt, mqttUser, mqttPassword))
//       {
//         Serial.println("MQTT connected");
//         client.subscribe(topicError);
//         client.subscribe(topicRestart);
//         client.subscribe(topicGetLogIdLoss);
//         client.subscribe(topicChange);
//         client.subscribe(topicShift);
//         vTaskDelay(pdMS_TO_TICKS(1000)); // Đợi trước khi thử lại
//       }
//       else
//       {
//         Serial.println("MQTT connection failed, retrying...");
//         vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi trước khi thử lại
//       }
//       // vTaskSuspend(wifiTaskHandle);  // Tạm dừng task WiFi khi Ethernet đã kết nối
//     }
//     else if (!ETH.linkUp() && ethPreviouslyConnected)
//     {
//       client.disconnect();
//       ethPreviouslyConnected = false; // Cập nhật trạng thái mất kết nối
//       Serial.println("Ethernet disconnected.");
//       // vTaskResume(wifiTaskHandle);  // Kích hoạt task WiFi khi Ethernet bị ngắt
//     }
//     vTaskDelay(10000 / portTICK_PERIOD_MS); // Chờ 1 giây
//   }
// }
/// @brief
void getInfoConnectMqtt()
{ 
  Serial.println("get data from server Settings MQTT");
  callAPIGetSettingsMqtt(settings, flashMutex);
  Serial.println(settings->MqttServer);
  Serial.println(settings->PortMqtt);
  client.setServer(settings->MqttServer, settings->PortMqtt);
  delay(1000);

  Serial.println("get data from server Company information");
  xTaskCreate(callAPIServerGetCompanyInfo, "getCompanyInfo", 2048, companyInfo, 2, NULL);
  listFiles(flashMutex);
  delay(4000);
  // Cập nhật thông tin của các topic
  strcpy(fullTopic, companyInfo->Mst);
  strcpy(topicStatus, companyInfo->Mst);
  strcpy(topicError, companyInfo->Mst);
  strcpy(topicRestart, companyInfo->Mst);
  strcpy(topicGetLogIdLoss, companyInfo->Mst);
  strcpy(topicShift, companyInfo->Mst);
  strcpy(topicChange, companyInfo->Mst);

  strcat(fullTopic, TopicSendData);
  strcat(topicStatus, TopicStatus);
  strcat(topicError, TopicLogError);
  strcat(topicRestart, TopicRestart);
  strcat(topicGetLogIdLoss, TopicGetLogIdLoss);
  // strcat(topicGetLogIdLoss, TopicGetLogIdLoss);
  strcat(topicShift, TopicShift);
  strcat(topicChange, TopicChange);

  // Nối chuỗi thứ hai vào mảng
  strcat(fullTopic, TopicMqtt);
  strcat(topicStatus, TopicMqtt);
  // strcat(topicGetLogIdLoss, TopicMqtt);

  Serial.println(fullTopic);
  Serial.println(topicGetLogIdLoss);
  Serial.println(companyInfo->CompanyId);
}

// Hàm xử lý tất cả các vòi
void processAllVoi(TimeSetup *time)
{
  for (int i = 0; i < numOfVoi; i++)
  {
    uint8_t idVoi = idVoiList[i];
    sendSetTimeCommand(idVoi, time); // Gửi lệnh cho ID vòi
    vTaskDelay(100/ portTICK_PERIOD_MS);                      // Chờ phản hồi từ thiết bị
    readResponse(idVoi);             // Đọc phản hồi
    vTaskDelay(100/ portTICK_PERIOD_MS);                      // Chờ phản hồi từ thiết bị
  }
}
/// @brief Check kết nối internet
// void checkInternetConnection()
// {
//   IPAddress ip(103, 57, 221, 161); // Google DNS
//   int pingResult = Ping.ping(ip);
//   // Serial.print("Ping: "); Serial.println(pingResult);
//   if (pingResult > 0)
//   {
//     // Serial.println("Ping successful - Internet is connected");
//     ethConnected = true;
//   }
//   else
//   {
//     // Serial.println("Ping failed - No Internet connection");
//     ethConnected = false;
//   }
// }

void checkHeap()
{
  if (millis() - timer > 10000)
  {
    timer = millis();
    deviceStatus->heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    deviceStatus->stack = uxTaskGetStackHighWaterMark(NULL);
    deviceStatus->free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    deviceStatus->temperature = temperatureRead();
    deviceStatus->counterReset = counterReset;
    strcpy(deviceStatus->idDevice, TopicMqtt);
    strcpy(deviceStatus->ipAddress, WiFi.localIP().toString().c_str());
    // strcpy(deviceStatus->status, "Running");

    JsonDocument doc;
    doc["heap"] = deviceStatus->heap;
    doc["stack"] = deviceStatus->stack;
    doc["free"] = deviceStatus->free;
    doc["temperature"] = deviceStatus->temperature;
    doc["counterReset"] = deviceStatus->counterReset;
    doc["idDevice"] = deviceStatus->idDevice;
    doc["ipAddress"] = deviceStatus->ipAddress;
    doc["status"] = deviceStatus->status;
    String json;
    serializeJson(doc, json);
    client.publish(topicStatus, json.c_str());
    // áp dụng cho board ASR
    tone(OUT2,300,100);
    // checkHeapIntegrity(); // Kiểm tra tính toàn vẹn của heap
  }
}

// Kích hoạt lại relay để nhấn O-E để thực hiện việc in lại dữ liệu
void ConnectedKPLBox(void * param) {
  // ĐOạn chương trình áp dụng cho ASR
  // tone(OUT2,1245,100);
  // vTaskDelay(200/ portTICK_PERIOD_MS);

  // ĐOạn chương trình áp dụng cho bản A2
  digitalWrite(OUT1, HIGH); // Bật relay
  vTaskDelay(200/ portTICK_PERIOD_MS);
  digitalWrite(OUT2, HIGH); // Bật relay
  vTaskDelay(200/ portTICK_PERIOD_MS);
  
  // không giữ trạng thái của rekay
  digitalWrite(OUT1, LOW); // Bật relay
  digitalWrite(OUT2, LOW);
  vTaskDelete(NULL);
}