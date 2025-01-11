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
#include <math.h>  // Thư viện math.h để sử dụng hàm floor(
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <structdata.h>
#include <ESP32Ping.h>

// Khởi tạo client

// Cấu hình Ethernet
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // Địa chỉ MAC duy nhất
WiFiClient  ethClient;                            // Ethernet client
PubSubClient client(ethClient);                      // MQTT client sử dụng Ethernet client
// Biến trạng thái cho kết nối và các tác vụ
bool ethConnected = false;
unsigned long counterReset = 0;
bool ethPreviouslyConnected = false;  // Cờ kiểm tra trạng thái kết nối trước đó


// Khai báo dữ liệu struct data
DeviceStatus *deviceStatus = new DeviceStatus;
CompanyInfo *companyInfo = new CompanyInfo; // lấy thông tin của company khi internet được kết nối
Settings *settings = new Settings;

// Thông tin chung của topic mqtt connected
char fullTopic[50]; // Đảm bảo kích thước đủ lớn
char topicStatus[50];
char topicError[50];
char topicRestart[50];
char topicGetLogIdLoss[50];
char topicShift[50];
char topicChange[50];
char shift[30];

// Biến toàn cục cho debounce
unsigned long lastDebounceTime = 0;  // Thời gian lần cuối thay đổi trạng thái
unsigned long debounceDelay = 50;   // Thời gian debounce (50ms)
int lastStableState = LOW;          // Trạng thái ổn định cuối cùng
int currentState = LOW;             // Trạng thái hiện tại

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
void readFlashSettings();

void runCommandRs485(void *parameter);
void readButton();
String trimExtraSpaces(String input);
String clearnData(String input);
void parseAndConvertToJson(String input) ;
void parseAndConvertToJson_2(String input);
String getValue(String data, String start, String end);
void mqttCallback(char *topic, byte *payload, unsigned int length);
void getIdLogLoss(void * param);
void parsePayload_IdLogStatus(byte *payload, unsigned int length, DeviceGetStatus *deviceGetStatus);
void ethernetTask(void *pvParameters);
void getInfoConnectMqtt();
void convertSettingsToHex(const Settings &settings, String &hexString);
void readSettingsInFlash(Settings &settings);
void callAPIServerGetCompanyInfo(void *param);
void callAPIServerGetCompanyInfo(void *param);
void convertSettingsFromHex(const String &hexString, Settings &settings);
void saveFileSettingsToFlash(const String &data);
void listFiles();
void callAPIGetSettingsMqtt(Settings *settings);
void checkInternetConnection();

/// @brief Hàm setup thiết lập các thông tin ban đầu
void setup() {

  Serial.begin(115200);
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL); // Thêm các nhiệm vụ bạn muốn giám sát, NULL để giám sát tất cả nhiệm vụ

  pinMode(OUT1, OUTPUT);         // Cài đặt OUT1 là đầu ra
  pinMode(OUT2, OUTPUT);         // Cài đặt OUT1 là đầu ra
  pinMode(INPUT1, INPUT_PULLUP);    // Cài đặt IN1 là đầu vào với Pull-up nội bộ

  // Initialize RS485
  Serial2.begin(RS485BaudRate, SERIAL_8N1, RX_PIN, TX_PIN);

  // Khởi động Ethernet
  Serial.println("Initializing Ethernet...");
  ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE);
  
  // Chờ Ethernet kết nối
  while (!ETH.linkUp()) {
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
  if (!LittleFS.begin())
  {
    Serial.println("Failed to initialize LittleFS, formatting...");
    formatLittleFS();
  }
  else
  {
    readFlashSettings();
  }

  Serial.println("Setup finished");

  xTaskCreate(ethernetTask, "ethernetTask", 8192, NULL, 1, &ethernetTaskhandle);

}

void loop() {

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
  if(strcmp(topic, topicRestart)==0){
    ESP.restart();
  }

  // Topic gởi thông tin ca bán hàng xuống deivice
  // if(strcmp(topic, topicShift)==0){
  //     parsePayload_ChangeShift(payload, length, &changeShift);
  //     return;
  // }

  // Topic gởi thông tin trạng thái của giao dịch ntn: Bán hàng, Test bồn, Đo bể
  // if(strcmp(topic, topicChange)==0){
  //     DeviceGetStatus deviceGetStatus;
  //     parsePayload_IdLogStatus(payload, length, &deviceGetStatus);
  //     if(strcmp(deviceGetStatus.Idvoi, TopicMqtt)==0){
  //         Serial.println("Da vao chuyen trang thai ban hang");
  //         xQueueSend(GetStatusDeviceQueue, &deviceGetStatus, pdMS_TO_TICKS(100));
  //     }
  //     return;
  // }

  if(strcmp(topic, topicError)==0){

    DeviceGetStatus deviceGetStatus;
    parsePayload_IdLogStatus(payload, length, &deviceGetStatus);
    Serial.println(deviceGetStatus.Idvoi);
    
    // char fullTopic[12];
    // strcpy(fullTopic, "");
    // strcat(fullTopic, TopicMqtt);
    // fullTopic[sizeof(fullTopic) - 1] = '\0'; // Đảm bảo chuỗi null-terminated
    // Serial.println(fullTopic);
    if(strcmp(deviceGetStatus.Idvoi, TopicMqtt) == 0){
        Serial.println("Lay lai thong tin giao dich");
        // Phat tin hieu
        xTaskCreate(getIdLogLoss, "LossLog", 1023, NULL, 3, &getIdLogLossHandle);
        // xQueueSend(GetStatusDeviceQueue, &deviceGetStatus, pdMS_TO_TICKS(100));
    }
  }
}

/// @brief hàm đọc tín hiệu input và phát ra tín hiệu in thông qua bàn phím
void readButton(){
  // Đọc trạng thái hiện tại của IN1
  int reading = digitalRead(INPUT1);

  // Nếu trạng thái thay đổi
  if (reading != lastStableState) {
    lastDebounceTime = millis(); // Ghi lại thời gian thay đổi
  }

  // Kiểm tra xem đã đủ thời gian debounce chưa
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Nếu trạng thái thực sự đã ổn định, cập nhật trạng thái hiện tại
    if (reading != currentState ) {
      currentState = reading;

      Serial.println(currentState);
      // Điều khiển relay theo trạng thái mới
      if (currentState == LOW) {
        digitalWrite(OUT1, LOW); // Bật relay
        digitalWrite(OUT2, LOW);
      } else {
        // Phat tin hieu
        digitalWrite(OUT1, HIGH); // Bật relay
        vTaskDelay(200);
        digitalWrite(OUT2, HIGH); // Bật relay
        vTaskDelay(200);
        
        // không giữ trạng thái của rekay
        digitalWrite(OUT1, LOW); // Bật relay
        digitalWrite(OUT2, LOW);
      }
    }
  }

  // Cập nhật trạng thái ổn định cuối cùng
  lastStableState = reading;

  // Một độ trễ nhỏ để ổn định vòng lặp
  // delay(10);
}

/// @brief Hàm đọc và xử lý tín hiệu
/// @param param 
void runCommandRs485(void *param)
{
  TickType_t getTick;
  getTick = xTaskGetTickCount();
  String rec = "";
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
    static int counter = 0;           // Đếm số chuỗi đã xử lý
    // Gọi hàm nhận dữ liệu
    
    while (Serial2.available() > 0) {
      String value = Serial2.readStringUntil('\n'); // Đọc từng dòng từ Serial2
      value = trimExtraSpaces(value);              // Loại bỏ khoảng trắng dư thừa
      value = clearnData(value); //

      // Chỉ xử lý chuỗi nếu độ dài hợp lệ
      if (value.length() > 10 && value.length() != 41) {
        value.trim(); // Loại bỏ khoảng trắng đầu/cuối chuỗi
        value.replace('\n', ' ');
        value.replace("--------------------------------", ""); // Xóa chuỗi không cần thiết
        value.replace("!Ea", " "); // Thay !Ea bằng ký tự xuống dòng
        value.replace("!0Ea", " "); // Thay !Ea bằng ký tự xuống dòng
        receivedString += value; // Ghép dữ liệu vào chuỗi chính
        counter++;               // Tăng bộ đếm

        Serial.print(value); Serial.print(" counter: "); Serial.print(counter); Serial.print(" length: "); Serial.println(receivedString.length());

        // Kiểm tra điều kiện hợp lệ để xử lý
        if (receivedString.endsWith("HEN GAP LAI") &&
            ((receivedString.length() >= 288 ) || (receivedString.length() >= 275 ) ||
            (receivedString.length() >= 314))) {
          parseAndConvertToJson(receivedString); // Gọi hàm phân tích và chuyển đổi sang JSON

          // Đặt lại các biến
          receivedString = "";
          counter = 0;
        }else if(receivedString.endsWith("Ben Ban Ben Mua") && receivedString.length()>360) {
          Serial.println(receivedString); //
          parseAndConvertToJson_2(receivedString);
          receivedString = "";
          counter = 0;
          // Serial.println(receivedString); // In ra chu);
        }
      }
    }

    vTaskDelayUntil(&getTick, 100 / portTICK_PERIOD_MS);
    continue;
  }
}


/// @brief Hàm parse ra json từ chuổi đọc được, nhưng áp dụng cho board mới của Montech mới có tốc độ truyền 115200
/// @param input 
void parseAndConvertToJson_2(String input) {
  JsonDocument doc;

  String maGiaoDich = getValue(input, "Ma lan bom:", "HOA DON BAN LE");
  String kyHieu = getValue(input, "Kieu:", "Ky Hieu:");
  String kyHieuMau = getValue(input, "Ky Hieu Mau:", "Ngay :");
  String ngay = getValue(input, "Ngay :", "Gio :");
  String gio = getValue(input, "Gio :", "So Serial :");
  String soSerial = getValue(input, "So Serial :", "Nhien lieu :");
  // String nhienLieu = getValue(input, "Nhien lieu:", "Tien :");
  String tien = getValue(input, "Tien :", "dongSo luong :");
  String soLuong = getValue(input, "So luong :", "litGia :");
  String gia = getValue(input, "Gia :", "dong/lit");

  // Tạo JSON
  doc["Id"] = maGiaoDich;
  doc["Ky_Hieu"] = kyHieu;
  doc["Ky_Hieu_Mau"] = kyHieuMau;
  doc["Ngay"] = ngay;
  doc["Gio"] = gio;
  doc["So_Seria"] = soSerial;
  doc["Type"] = companyInfo->Product;

  doc["Money"] = tien;
  doc["Amount"] = soLuong;
  doc["Price"] = gia;

  // Serialize JSON to String
  String jsonString;
  serializeJson(doc, jsonString);
  jsonString += '\0';  // Thêm null-terminator

  // Serial.println(jsonString);
 
  // Send JSON to MQTT queue
  char *jsonCopy = (char *)pvPortMalloc(jsonString.length() + 1);
  if (jsonCopy != NULL) {
    strcpy(jsonCopy, jsonString.c_str()); // Copy JSON string to allocated memory

    // Send pointer to queue
    if (xQueueSend(mqttQueue, &jsonCopy, pdMS_TO_TICKS(100)) != pdPASS) {
      Serial.println("Error: Failed to send JSON pointer to MQTT queue");
      vPortFree(jsonCopy); // Free memory if not sent
    } else {
      Serial.println("JSON sent to queue successfully");
    }
  } else {
    Serial.println("Error: Memory allocation failed");
  }
}

/// @brief Hàm xử lý parse json từ đoạn chuổi nhận được áp dụng cho board có tốc độ truyền 19200 và chiều dài chuổi bé
/// @param input 
void parseAndConvertToJson(String input) {
  JsonDocument doc;

  String maGiaoDich = getValue(input, "Ma giao dich:", "HOA DON BAN");
  String kyHieu = getValue(input, "Kieu:", "Ky Hieu:");
  String kyHieuMau = getValue(input, "Ky Hieu Mau:", "Ngay :");
  String ngay = getValue(input, "Ngay :", "Gio :");
  String gio = getValue(input, "Gio :", "So Serial :");
  String soSerial = getValue(input, "So Serial :", "Nhien lieu:");
  // String nhienLieu = getValue(input, "Nhien lieu:", "Tien :");
  String tien = getValue(input, "Tien :", "dongSo luong :");
  String soLuong = getValue(input, "So luong :", "litGia :");
  String gia = getValue(input, "Gia :", "dong/lit");

  // Tạo JSON
  doc["Id"] = maGiaoDich;
  doc["Ky_Hieu"] = kyHieu;
  doc["Ky_Hieu_Mau"] = kyHieuMau;
  doc["Ngay"] = ngay;
  doc["Gio"] = gio;
  doc["So_Seria"] = soSerial;
  doc["Type"] = companyInfo->Product;

  doc["Money"] = tien;
  doc["Amount"] = soLuong;
  doc["Price"] = gia;

  // Serialize JSON to String
  String jsonString;
  serializeJson(doc, jsonString);

  // Serial.println(jsonString);
  // jsonString += '\0';  // Thêm null-terminator
  // Serial.println("Str: " +  jsonString + " len: " + jsonString.length());

  // // Send JSON to MQTT queue
  // if (xQueueSend(mqttQueue, &jsonString, pdMS_TO_TICKS(100)) != pdPASS) {
  //     Serial.println("Error: Failed to send JSON to MQTT queue");
  // }

  // Allocate memory for JSON string using pvPortMalloc
  char *jsonCopy = (char *)pvPortMalloc(jsonString.length() + 1);
  if (jsonCopy != NULL) {
    strcpy(jsonCopy, jsonString.c_str()); // Copy JSON string to allocated memory
 
    // Send pointer to queue
    if (xQueueSend(mqttQueue, &jsonCopy, pdMS_TO_TICKS(100)) != pdPASS) {
      Serial.println("Error: Failed to send JSON pointer to MQTT queue");
      vPortFree(jsonCopy); // Free memory if not sent
    } else {
      Serial.println("JSON sent to queue successfully");
    }
  } else {
    Serial.println("Error: Memory allocation failed");
  }
}

/// @brief Hàm dùng để tách chuổi đọc được, lấy giá trị từ chuổi đọc được, giống hàm Mid trong excel
/// @param data : Chuổi cần đọc
/// @param start: Vị trí bắt đầu cắt chuổi
/// @param end : Vi trí kết thúc chuổi
/// @return 
String getValue(String data, String start, String end) {
  int startIndex = data.indexOf(start);
  if (startIndex == -1) return ""; // Không tìm thấy từ khóa bắt đầu
  startIndex += start.length();
  int endIndex = data.indexOf(end, startIndex);
  if (endIndex == -1) return ""; // Không tìm thấy từ khóa kết thúc
  String result= data.substring(startIndex, endIndex);
  result.trim();
  return result;
}

/// @brief Lọc các ký tự lạ trong serial
/// @param input 
/// @return 
String clearnData(String input) {
    String result = "";
    for (int i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c >= 32 && c <= 126) { // ASCII từ 32 đến 126 là ký tự in được
            result += c;
        }
    }
    input.replace("!Ea", ""); // Thay !Ea bằng ký tự xuống dòng
    return result;
}

/// @brief Hàm để phân tích payload và gán vào struct GetIdLogLoss
/// @param payload 
/// @param length 
/// @param deviceGetStatus 
void parsePayload_IdLogStatus(byte *payload, unsigned int length, DeviceGetStatus *deviceGetStatus) {
  // Tạo một buffer cho payload để xử lý với ArduinoJson
  char json[length + 1];
  memcpy(json, payload, length);
  json[length] = '\0'; // Đảm bảo chuỗi kết thúc

  // Khởi tạo đối tượng JSON document
  JsonDocument doc;

  // Phân tích chuỗi JSON
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }
  double counter = doc["Status"] | 0;
  deviceGetStatus->Status = counter;

  // Gán dữ liệu từ JSON vào các trường của getLogId
  strlcpy(deviceGetStatus->Idvoi, doc["Idvoi"] | "", sizeof(deviceGetStatus->Idvoi));
  strlcpy(deviceGetStatus->Request_Code, doc["Request_Code"] | "", sizeof(deviceGetStatus->Request_Code));
}
/// Kích hoạt lại relay để nhấn O-E để thực hiện việc in lại dữ liệu
void getIdLogLoss(void * param) {
   vTaskDelay(400/ portTICK_PERIOD_MS);
  digitalWrite(OUT1, HIGH); // Bật relay
  vTaskDelay(200/ portTICK_PERIOD_MS);
  digitalWrite(OUT2, HIGH); // Bật relay
  vTaskDelay(200/ portTICK_PERIOD_MS);
  
  // không giữ trạng thái của rekay
  digitalWrite(OUT1, LOW); // Bật relay
  digitalWrite(OUT2, LOW);
  vTaskDelete(NULL);
}

/// @brief Task dùng để kiểm tra việc kết nối internet
/// @param pvParameters 
void ethernetTask(void *pvParameters) {
    while (true) {
        checkInternetConnection();
        if (ETH.linkUp() && ethConnected && !ethPreviouslyConnected) {
            getInfoConnectMqtt();
            ethPreviouslyConnected = true;  // Cập nhật trạng thái đã kết nối
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
        } else if (!ETH.linkUp() && ethPreviouslyConnected) {
            client.disconnect();
            ethPreviouslyConnected = false;  // Cập nhật trạng thái mất kết nối
            Serial.println("Ethernet disconnected.");
            // vTaskResume(wifiTaskHandle);  // Kích hoạt task WiFi khi Ethernet bị ngắt
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Chờ 1 giây
    }
}
/// @brief 
void getInfoConnectMqtt(){
    Serial.println("get data from server Settings MQTT");

    callAPIGetSettingsMqtt(settings);
    Serial.println(settings->MqttServer);
    Serial.println(settings->PortMqtt);
    client.setServer(settings->MqttServer, settings->PortMqtt);
    delay(1000);

    Serial.println("get data from server Company information");
    xTaskCreate(callAPIServerGetCompanyInfo, "getCompanyInfo", 2048, companyInfo, 2, NULL);
    listFiles();
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

/// @brief Hàm lấy thông tin từ server và kiểm tra nội dung có thay đổi trong flash hay không? Nếu có lưu mới
/// @param settings 
void callAPIGetSettingsMqtt(Settings *settings)
{
  // Settings *settings = (Settings *)param;
  // cần đọc data từ file companyInfo.txt lên trước
  Settings settingsInFlash;
  readSettingsInFlash(settingsInFlash);

  if (ETH.linkUp())
  {
    HTTPClient http;
    String url = "http://103.57.221.161:5002/companys-managerment/getMqttServer";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Prepare JSON data to send
    JsonDocument doc;
    doc["IdDevice"] = TopicMqtt;

    String json;
    serializeJson(doc, json);

    // Send POST request
    int httpResponseCode = http.POST(json.c_str());

    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      String response = http.getString();
      JsonDocument doc; // Adjust size based on expected response
      DeserializationError error = deserializeJson(doc, response);

      if (!error)
      {
        // Parse the JSON array
        JsonArray array = doc.as<JsonArray>();
        if (array.size() > 0)
        {
          JsonObject obj = array[0];

          // Copy CompanyId into char array
          strlcpy(settings->MqttServer, obj["MqttServer"], sizeof(settings->MqttServer));
          settings->PortMqtt = obj["PortMqtt"];

          Serial.println("MqttServer: " + String(settings->MqttServer));
          Serial.println("PortMqtt: " + String(settings->PortMqtt));
          // Chuyển đổi data company qua dạng hex
          String hexString;
          convertSettingsToHex(*settings, hexString);
          Serial.println("Data hexString: " + hexString + " length: " + hexString.length());
          // Cần đọc dữ liệu đã lưu trong file companyInfo.txt
          if (strcmp(settingsInFlash.MqttServer, settings->MqttServer) == 0 && settingsInFlash.PortMqtt == settings->PortMqtt)
          {
            Serial.println("Data Settings not new");
          }
          else
          {
            Serial.println("Save data Settings new to flash");
            saveFileSettingsToFlash(hexString);
          }
        }
        else
        {
          strcpy(settings->MqttServer, settingsInFlash.MqttServer);
          settings->PortMqtt = settingsInFlash.PortMqtt;
          Serial.println("No data in JSON array");
        }
      }
      else
      {
        strcpy(settings->MqttServer, settingsInFlash.MqttServer);
        settings->PortMqtt = settingsInFlash.PortMqtt;
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
      }
    }
    else
    {
      strcpy(settings->MqttServer, settingsInFlash.MqttServer);
      settings->PortMqtt = settingsInFlash.PortMqtt;
      Serial.print("Error on sending POST request: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
  else
  {
    // convertFromHex(dataCompanyInfo, *company);
    ethConnected = false;
    Serial.println("Khong ket noi duoc internet");
  }

  Serial.println("MqttServer: " + String(settings->MqttServer));
  Serial.println("PortMqtt: " + String(settings->PortMqtt));
  // vTaskDelete(NULL);
}

/// @brief Đọc thông tin thiết lập trong bộ nhớ Falash
/// @param settings 
void readSettingsInFlash(Settings &settings)
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");
      xSemaphoreGive(flashMutex);
      strcpy(deviceStatus->status, "Failed to mount file system");
      return;
    }
    if (!LittleFS.exists("/settings.txt"))
    {
      Serial.println("File settings.txt not found");
      strcpy(deviceStatus->status, "File settings.txt not found");
      File file = LittleFS.open("/settings.txt", FILE_WRITE);
      file.close();
    }
    File file = LittleFS.open("/settings.txt", FILE_READ);
    if (!file)
    {
      Serial.println("Failed to open file settings.txt for reading");
      strcpy(deviceStatus->status, "Failed read settings.txt");
      file.close();
      xSemaphoreGive(flashMutex);
      return;
    }

    String line = file.readStringUntil('\n');
    file.close();
    xSemaphoreGive(flashMutex);
    convertSettingsFromHex(line, settings);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
  strcpy(deviceStatus->status, "Failed to take flash mutex");
}
/// @brief Định nghĩa lại chuổi setting qua dạng hexa để lưu tối ưu bộ nhớ
/// @param settings 
/// @param hexString 
void convertSettingsToHex(const Settings &settings, String &hexString)
{
  char buffer[5]; // Đủ để chứa 4 ký tự hexa cho PortMqtt và ký tự null
  char buffer2[3];
  hexString.clear(); // Xóa chuỗi hexString để đảm bảo nó trống trước khi thêm vào

  // Tối ưu hóa bộ nhớ bằng cách sử dụng một biến tạm thời
  String tempHex;

  // Chuyển đổi MqttServer sang dạng hex
  for (int i = 0; i < sizeof(settings.MqttServer); i++)
  {
    sprintf(buffer2, "%02X", (unsigned char)settings.MqttServer[i]);
    tempHex += buffer2;
  }

  // Chuyển đổi PortMqtt sang dạng hexa 4 ký tự
  sprintf(buffer, "%04X", settings.PortMqtt);
  tempHex += buffer;

  hexString = tempHex; // Gán một lần vào hexString để giảm số lần cập nhật chuỗi
}

/// @brief Hàm lấy thông tin dữ liệu vòi bom cho từng thiết bị từ server qua API
/// @param param 
void callAPIServerGetCompanyInfo(void *param)
{
  CompanyInfo *company = (CompanyInfo *)param;
  if (ETH.linkUp())
  {
    HTTPClient http;
    String url = "http://103.57.221.161:5002/device-managerment/devices/infoid";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Prepare JSON data to send
    JsonDocument doc;
    doc["IdDevice"] = TopicMqtt;

    String json;
    serializeJson(doc, json);

    // Send POST request
    int httpResponseCode = http.POST(json.c_str());

    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      String response = http.getString();
      JsonDocument doc; // Adjust size based on expected response
      DeserializationError error = deserializeJson(doc, response);

      if (!error)
      {
        // Parse the JSON array
        JsonArray array = doc.as<JsonArray>();
        if (array.size() > 0)
        {
          JsonObject obj = array[0];

          // Copy CompanyId into char array
          strlcpy(company->CompanyId, obj["CompanyId"], sizeof(company->CompanyId));
          strlcpy(company->Mst, obj["Mst"], sizeof(company->Mst));

          strlcpy(company->Product, obj["Product"], sizeof(company->Product));

          strcpy(companyInfo->CompanyId, company->CompanyId);
          strcpy(companyInfo->Mst, company->Mst);
          strcpy(companyInfo->Product, company->Product);

          Serial.println("CompanyId: " + String(company->CompanyId));
          Serial.println("Mst: " + String(company->Mst));
          Serial.println("Product: " + String(company->Product));
        }
        else
        {
          Serial.println("No data in JSON array");
        }
      }
      else
      {
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
      }
    }
    else
    {
      Serial.print("Error on sending POST request: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
  else
  {
    Serial.println("Không có kết nối WiFi");
  }
  vTaskDelete(NULL);
}
/// @brief Hàm đọc thông tin lưu dữ liệu trong flash
void listFiles()
{
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");

      xSemaphoreGive(flashMutex);
      return;
    }

    Serial.println("Listing files:");
    File root = LittleFS.open("/");
    if (!root)
    {
      Serial.println("Failed to open root directory");
      xSemaphoreGive(flashMutex);
      return;
    }
    File file = root.openNextFile();
    if (!file)
    {
      Serial.println("Failed to open file");
      root.close();
      LittleFS.end();
      xSemaphoreGive(flashMutex);
      return;
    }
    while (file)
    {
      Serial.print("File: ");
      Serial.print(file.name());
      Serial.print(" - Size: ");
      Serial.println(file.size());
      file = root.openNextFile();
    }
    file.close();
    root.close();
    LittleFS.end();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
}

/// @brief Convert Settings hexa qua Settings String
/// @param hexString 
/// @param settings 
void convertSettingsFromHex(const String &hexString, Settings &settings)
{
  int len = hexString.length();
  int mqttServerSize = sizeof(settings.MqttServer) * 2;
  memset(settings.MqttServer, 0, sizeof(settings.MqttServer) + 1); // Clear previous data

  for (int i = 0; i < len; i += 2)
  {
    String byteString = hexString.substring(i, i + 2);
    char byte = (char)strtol(byteString.c_str(), NULL, 16);

    if (i < mqttServerSize)
    {
      settings.MqttServer[i / 2] = byte;
    }
  }
  String portHexString = hexString.substring(len - 4, len);    // Get last 8 characters for 4 bytes
  settings.PortMqtt = strtol(portHexString.c_str(), NULL, 16); // Convert hex to decimal
  settings.MqttServer[sizeof(settings.MqttServer) - 1] = '\0'; // Ensure null termination
}
/// @brief Lưu nội dung settings file vào bộ nhớ Flash
/// @param data 
void saveFileSettingsToFlash(const String &data)
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");
      strcpy(deviceStatus->status, "Failed to mount file system");
      xSemaphoreGive(flashMutex);
      return;
    }

    File file = LittleFS.open("/settings.txt", FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to open file for writing");
      strcpy(deviceStatus->status, "Failed to open file for writing");
      file.close();
      xSemaphoreGive(flashMutex);
      return;
    }
    if (file.print(data.c_str()))
    { // Sử dụng printf để lưu dữ liệu
      Serial.println("File written successfully");
    }
    else
    {
      Serial.println("Write failed");
      strcpy(deviceStatus->status, "Write failed");
    }
    file.close();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
    strcpy(deviceStatus->status, "Failed to take flash mutex");
  }
}
/// @brief Format thông tin Flash
void formatLittleFS()
{
  if (LittleFS.format())
  {
    Serial.println("LittleFS formatted successfully");
  }
  else
  {
    Serial.println("Failed to format LittleFS");
  }
}
/// @brief Đọc các thông tin thiết lập trong bộ nhớ Flash
void readFlashSettings()
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to initialize LittleFS");
      xSemaphoreGive(flashMutex);
      strcpy(deviceStatus->status, "Failed to initialize LittleFS");
      return;
    }
    if (!LittleFS.exists("/counter.bin"))
    {
      Serial.println("File counter.bin not found");
      File file = LittleFS.open("/counter.bin", FILE_WRITE);
      file.close();
      strcpy(deviceStatus->status, "File counter.bin not found");
    }

    File file = LittleFS.open("/counter.bin", "r");
    if (!file)
    {
      Serial.println("Failed to read from flash");
      strcpy(deviceStatus->status, "Failed to read from flash");
      xSemaphoreGive(flashMutex);
      return;
    }

    file.readBytes((char *)&counterReset, sizeof(counterReset));
    file.close();
    counterReset = counterReset + 1;
    file = LittleFS.open("/counter.bin", "w");
    if (!file)
    {
      Serial.println("Failed to write to flash");
      strcpy(deviceStatus->status, "Failed to write to flash");
      xSemaphoreGive(flashMutex);
      return;
    }

    file.write((const uint8_t *)&counterReset, sizeof(counterReset));
    file.close();
    Serial.println(counterReset);
    LittleFS.end();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
  strcpy(deviceStatus->status, "Failed to take flash mutex");
}
/// @brief Check kết nối internet
void checkInternetConnection() {
    IPAddress ip(103,57,221,161);  // Google DNS
    int pingResult = Ping.ping(ip);
    // Serial.print("Ping: "); Serial.println(pingResult);
    if (pingResult > 0) {
        // Serial.println("Ping successful - Internet is connected");
        ethConnected = true;
    } else {
        // Serial.println("Ping failed - No Internet connection");
        ethConnected = false;
    }
}