#include <Wire.h>
#include <ETH.h>
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

// Khởi tạo client

// Cấu hình Ethernet
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // Địa chỉ MAC duy nhất
WiFiClient ethClient;                              // Ethernet client
PubSubClient client(ethClient);                    // MQTT client sử dụng Ethernet client
// Biến trạng thái cho kết nối và các tác vụ
bool ethConnected = false;
unsigned long counterReset = 0;
bool ethPreviouslyConnected = false; // Cờ kiểm tra trạng thái kết nối trước đó

// Khai báo dữ liệu struct data
DeviceStatus *deviceStatus = new DeviceStatus;
CompanyInfo *companyInfo = new CompanyInfo; // lấy thông tin của company khi internet được kết nối
Settings *settings = new Settings;
uint32_t currentId = 0; // ID vô hạn

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
TaskHandle_t ethernetTaskhandle = NULL;

// Function prototypes
void formatLittleFS();
void runCommandRs485(void *parameter);
void parseAndConvertToJson(String input);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void getIdLogLoss(void *param);
void ethernetTask(void *pvParameters);
void getInfoConnectMqtt();
void checkInternetConnection();
void sendDataCommand(String command, char *&param);

/// @brief Hàm setup thiết lập các thông tin ban đầu
void setup()
{
  Serial.begin(115200);
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL); // Thêm các nhiệm vụ bạn muốn giám sát, NULL để giám sát tất cả nhiệm vụ
  // Initialize RS485
  Serial2.begin(RS485BaudRate, SERIAL_8N1, RX_PIN, TX_PIN);

  // Khởi động Ethernet
  Serial.println("Initializing Ethernet...");
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);

  // Chờ Ethernet kết nối
  while (!ETH.linkUp())
  {
    Serial.println("Waiting for Ethernet connection...");
    delay(1000);
  }
  ethConnected = true;

  // Hiển thị thông tin kết nối
  Serial.print("Connected! IP address: ");
  Serial.println(ETH.localIP());

  // Cấu hình MQTT server (thay thế bằng địa chỉ MQTT Broker của bạn)
  client.setServer(mqttServer, 1883);
  client.setCallback(mqttCallback);

  // Khởi tạo Queue ban đầu
  mqttQueue = xQueueCreate(15, 125 * sizeof(char));
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
  xTaskCreate(ethernetTask, "ethernetTask", 8192, NULL, 1, &ethernetTaskhandle);
}

void loop()
{

  // Kiểm tra thông tin của thiết bị cơ bản: Head memory

  if (ETH.linkUp() && ethConnected)
  {
    /* code */
    client.loop();
  }

  esp_task_wdt_reset();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  yield();
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
    ESP.restart();
  }
  if (strcmp(topic, topicError) == 0)
  {
    DeviceGetStatus deviceGetStatus;
    parsePayload_IdLogStatus(payload, length, &deviceGetStatus);
    Serial.println(deviceGetStatus.Idvoi);

    if (strcmp(deviceGetStatus.Idvoi, TopicMqtt) == 0)
    {
      Serial.println("Lay lai thong tin giao dich");
      // Phat tin hieu
      xTaskCreate(getIdLogLoss, "LossLog", 1023, NULL, 3, &getIdLogLossHandle);
      // xQueueSend(GetStatusDeviceQueue, &deviceGetStatus, pdMS_TO_TICKS(100));
    }
  }
}

void sendDataCommand(String command, char *&param)
{
  char *tempdata = (char *)pvPortMalloc(ITEM_SIZE_RS485 * sizeof(char));
  if (tempdata == NULL)
  {
    Serial.println("Error: Failed to allocate memory for tempdata");
    strcpy(deviceStatus->status, "Failed Malock memory");
    return;
  }
  // Serial.println("Lenh Commands: " +command);
  // RS485Serial.flush(); // Đảm bảo bộ đệm truyền trước đó đã được xóa
  Serial2.print(command); //, leng);
  // Kiểm tra xem dữ liệu đã được gửi đi chưa
  Serial2.flush(); // Đợi cho đến khi toàn bộ dữ liệu được truyền đi

  // Kiểm tra lại bộ đệm truyền
  if (Serial2.availableForWrite() < command.length())
  {
    Serial.println("Du lieu goi bi mat");
  }
  byte index = 0;

  vTaskDelay(150 / portTICK_PERIOD_MS); // Thêm độ trễ ngắn trước khi đọc phản hồidf mặc định là 200
  while (Serial2.available() > 0)
  {
    char currentChar = Serial2.read();
    tempdata[index++] = currentChar;
  }
  tempdata[index++] = '\0';

  // Serial.println(tempdata);

  param = (char *)pvPortMalloc((index + 1) * sizeof(char)); // Cấp phát vùng nhớ mới đủ chứa dữ liệu
  if (param != NULL)
  {
    strcpy(param, tempdata); // Sao chép dữ liệu
    // Serial.print("data: "); Serial.println(param);
  }
  else
  {
    Serial.println("Error: Failed to allocate memory for param");
    strcpy(deviceStatus->status, "Failed Malock memory");
  }
  // Giải phóng tempdata sau khi sao chép
  vPortFree(tempdata);
}

/// @brief Hàm đọc và xử lý tín hiệu
/// @param param
void runCommandRs485(void *param)
{
  TickType_t getTick;
  getTick = xTaskGetTickCount();
  uint8_t buffer[LOG_SIZE];

  while (true)
  {
    esp_task_wdt_reset();
    if (!Serial2 || !Serial2.availableForWrite())
    {
      Serial.println("Cổng Rs485 Chưa sẳn sàng\n");
      vTaskDelay(500 / portTICK_PERIOD_MS);
      // continue;
    }
    static String receivedString = ""; // Biến lưu chuỗi nhận được
    static int counter = 0;            // Đếm số chuỗi đã xử lý
    // Gọi hàm nhận dữ liệu
    if (Serial2.available() > LOG_SIZE)
    {
      // Đọc 32 byte
      Serial2.readBytes(buffer, LOG_SIZE);
      // Kiểm tra checksum
      uint8_t calculatedChecksum = calculateChecksum_LogData(buffer, LOG_SIZE);

      PumpLog log;
      log.checksum = buffer[29];
      // Kiểm tra điều kiện log gởi lên có đúng hay ko
      if (calculatedChecksum == log.checksum)
      {

        // Save Buffer Log in to Flash Esp32.

        // Gán dữ liệu vào struct
        ganLog(buffer, log);

        String jsondata = convertPumpLogToJson(log);
        // Allocate memory for JSON string using pvPortMalloc
        char *jsonCopy = (char *)pvPortMalloc(jsondata.length() + 1);
        if (jsonCopy != NULL)
        {
          strcpy(jsonCopy, jsondata.c_str()); // Copy JSON string to allocated memory
          // Send pointer to queue
          if (xQueueSend(mqttQueue, &jsonCopy, pdMS_TO_TICKS(100)) != pdPASS)
          {
            Serial.println("Error: Failed to send JSON pointer to MQTT queue");
            vPortFree(jsonCopy); // Free memory if not sent
          }
          else
          {
            Serial.println("JSON sent to queue successfully");
          }
        }
        else
        {
          Serial.println("Error: Memory allocation failed");
        }
      }
    }

    vTaskDelayUntil(&getTick, 100 / portTICK_PERIOD_MS);
    continue;
  }
}


/// Kích hoạt lại relay để nhấn O-E để thực hiện việc in lại dữ liệu
void getIdLogLoss(void *param)
{
  Serial.println("Chuong trinh lay lop loss");
  vTaskDelete(NULL);
}

/// @brief Task dùng để kiểm tra việc kết nối internet
/// @param pvParameters
void ethernetTask(void *pvParameters)
{
  while (true)
  {
    checkInternetConnection();
    if (ETH.linkUp() && ethConnected && !ethPreviouslyConnected)
    {
      getInfoConnectMqtt();
      ethPreviouslyConnected = true; // Cập nhật trạng thái đã kết nối
      Serial.println("Ethernet connected on interneted");

      Serial.println("Connecting to MQTT...");
      if (client.connect(TopicMqtt, mqttUser, mqttPassword))
      {
        Serial.println("MQTT connected");
        client.subscribe(topicError);
        client.subscribe(topicRestart);
        client.subscribe(topicGetLogIdLoss);
        client.subscribe(topicChange);
        client.subscribe(topicShift);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Đợi trước khi thử lại
      }
      else
      {
        Serial.println("MQTT connection failed, retrying...");
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi trước khi thử lại
      }
      // vTaskSuspend(wifiTaskHandle);  // Tạm dừng task WiFi khi Ethernet đã kết nối
    }
    else if (!ETH.linkUp() && ethPreviouslyConnected)
    {
      client.disconnect();
      ethPreviouslyConnected = false; // Cập nhật trạng thái mất kết nối
      Serial.println("Ethernet disconnected.");
      // vTaskResume(wifiTaskHandle);  // Kích hoạt task WiFi khi Ethernet bị ngắt
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Chờ 1 giây
  }
}
/// @brief
void getInfoConnectMqtt()
{
  Serial.println("get data from server Settings MQTT");
  callAPIGetSettingsMqtt(settings, flashMutex, ethConnected);
  Serial.println(settings->MqttServer);
  Serial.println(settings->PortMqtt);
  client.setServer(settings->MqttServer, settings->PortMqtt);
  delay(1000);

  Serial.println("get data from server Company information");
  xTaskCreate(callAPIServerGetCompanyInfo, "getCompanyInfo", 2048, companyInfo, 2, NULL);
  listFiles(flashMutex);
  delay(4000);
  // Cập nhật thông tin của các topic
  strcpy(fullTopic, companyInfo->CompanyId);
  strcpy(topicStatus, companyInfo->CompanyId);
  strcpy(topicError, companyInfo->CompanyId);
  strcpy(topicRestart, companyInfo->CompanyId);
  strcpy(topicGetLogIdLoss, companyInfo->CompanyId);
  strcpy(topicShift, companyInfo->CompanyId);
  strcpy(topicChange, companyInfo->CompanyId);

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

/// @brief Check kết nối internet
void checkInternetConnection()
{
  IPAddress ip(103, 57, 221, 161); // Google DNS
  int pingResult = Ping.ping(ip);
  // Serial.print("Ping: "); Serial.println(pingResult);
  if (pingResult > 0)
  {
    // Serial.println("Ping successful - Internet is connected");
    ethConnected = true;
  }
  else
  {
    // Serial.println("Ping failed - No Internet connection");
    ethConnected = false;
  }
}