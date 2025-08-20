#ifndef API_H
#define API_H
#include <Arduino.h>
#include "structdata.h"
#include "FlashFile.h"
#include <ETH.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/queue.h>
#include "Credentials.h"

/// @brief Hàm lấy thông tin từ server và kiểm tra nội dung có thay đổi trong flash hay không? Nếu có lưu mới
/// @param settings
void callAPIGetSettingsMqtt(Settings *settings, SemaphoreHandle_t flashMutex)
{
  // Settings *settings = (Settings *)param;
  // cần đọc data từ file companyInfo.txt lên trước
  Settings settingsInFlash;
  readSettingsInFlash(settingsInFlash, flashMutex);

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String url = String(API_BASE_URL) + API_SETTINGS_ENDPOINT;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Prepare JSON data to send
    DynamicJsonDocument doc(1024); // Adjust size based on expected response
    doc["IdDevice"] = TopicMqtt;

    String json;
    serializeJson(doc, json);

    // Send POST request
    int httpResponseCode = http.POST(json.c_str());

    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      String response = http.getString();
      DynamicJsonDocument doc(2048); // allow both array/object
      DeserializationError error = deserializeJson(doc, response);

      if (!error)
      {
        JsonObject obj;
        if (doc.is<JsonArray>()) {
          JsonArray array = doc.as<JsonArray>();
          if (array.size() > 0) obj = array[0];
        } else if (doc.is<JsonObject>()) {
          obj = doc.as<JsonObject>();
        }

        if (!obj.isNull())
        {
          strlcpy(settings->MqttServer, obj["MqttServer"] | settingsInFlash.MqttServer, sizeof(settings->MqttServer));
          settings->PortMqtt = obj["PortMqtt"] | settingsInFlash.PortMqtt;

          // Serial.println("MqttServer: " + String(settings->MqttServer));
          // Serial.println("PortMqtt: " + String(settings->PortMqtt));
          String hexString;
          convertSettingsToHex(*settings, hexString);
          Serial.println("Data hexString: " + hexString + " length: " + hexString.length());
          if (strcmp(settingsInFlash.MqttServer, settings->MqttServer) == 0 && settingsInFlash.PortMqtt == settings->PortMqtt)
          {
            Serial.println("Data Settings not new");
          }
          else
          {
            Serial.println("Save data Settings new to flash");
            saveFileSettingsToFlash(hexString, flashMutex);
          }
        }
        else
        {
          strcpy(settings->MqttServer, settingsInFlash.MqttServer);
          settings->PortMqtt = settingsInFlash.PortMqtt;
          Serial.println("No data from API, using cached settings");
        }
      }
      else
      {
        strcpy(settings->MqttServer, settingsInFlash.MqttServer);
        settings->PortMqtt = settingsInFlash.PortMqtt;
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
        Serial.println(response);
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
    // ethConnected = false;
    Serial.println("Khong ket noi duoc internet");
  }

  Serial.println("MqttServer: " + String(settings->MqttServer));
  Serial.println("PortMqtt: " + String(settings->PortMqtt));
  // vTaskDelete(NULL);
}

/// @brief Hàm lấy thông tin dữ liệu vòi bom cho từng thiết bị từ server qua API
/// @param param
void callAPIServerGetCompanyInfo(void *param)
{
  CompanyInfo *company = (CompanyInfo *)param;
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String url = String(API_BASE_URL) + API_COMPANY_ENDPOINT;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    // Prepare JSON data to send
    DynamicJsonDocument doc(1024);
    doc["IdDevice"] = TopicMqtt;

    Serial.println("json info: " + String(TopicMqtt));

    String json;
    serializeJson(doc, json);

    // Send POST request
    int httpResponseCode = http.POST(json.c_str());

    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      String response = http.getString();
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, response);

      if (!error)
      {
        JsonObject obj;
        if (doc.is<JsonArray>()) {
          JsonArray array = doc.as<JsonArray>();
          if (array.size() > 0) obj = array[0];
        } else if (doc.is<JsonObject>()) {
          obj = doc.as<JsonObject>();
        }

        if (!obj.isNull())
        {
          strlcpy(company->CompanyId, obj["CompanyId"] | company->CompanyId, sizeof(company->CompanyId));
          strlcpy(company->Mst, obj["Mst"] | company->Mst, sizeof(company->Mst));
          strlcpy(company->Product, obj["Product"] | company->Product, sizeof(company->Product));

          Serial.println("CompanyId: " + String(company->CompanyId));
          Serial.println("Mst: " + String(company->Mst));
          Serial.println("Product: " + String(company->Product));
        }
        else
        {
          Serial.println("No company data from API");
        }
      }
      else
      {
        Serial.print("Error parsing JSON: ");
        Serial.println(error.c_str());
        Serial.println(response);
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


/// @brief Chương trình lấy logid bị mất dựa trên counter- Sql server trả về là giá trị counter bị mất. từ Counter này truy vấn qua Idloger trong bộ nhớ của bộ số
/// @param param
void callAPIServerGetLogLoss(void *param){
  TaskParams *params = (TaskParams *)param; // Ép kiểu về TaskParams
  GetIdLogLoss *msg = params->msg;
  QueueHandle_t LogIdLossQueue = params->logIdLossQueue;

  // Serial.printf("id: %s \n", msg.Idvoi);
  // Serial.printf("Today: %s \n", msg.Today);
  // Serial.printf("Request_Code: %s\n", msg.Request_Code);
  // Serial.printf("CompanyId: %s\n", msg.CompanyId);

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    // Your API endpoint
    String url = String(API_BASE_URL) + API_LOG_LOSS_ENDPOINT;
    http.begin(url);
    // Headers
    http.addHeader("Content-Type", "application/json");
    // Check heap before API call
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    Serial.printf("API task heap: %u bytes\n", freeHeap);
    
    // JSON data - reduced size
    DynamicJsonDocument doc(1024);
    doc["idvoi"] = msg->Idvoi;
    doc["today"] = msg->Today;
    doc["request_code"] = msg->Request_Code;
    doc["company"] = msg->CompanyId;

    String json;
    serializeJson(doc, json);

    Serial.print("json:" + json +"\n");
    // Send the POST request
    int httpResponseCode = http.POST(json.c_str());

    // show nội dụng gởi về từ http
    String response = http.getString();
    // Serial.printf("json: %s\n", response.c_str());


    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      // Auto-size document capacity based on response length
      size_t respLen = response.length();
      Serial.printf("API response length: %u\n", (unsigned)respLen);

      const size_t maxCapacity = 16384; // upper bound
      size_t capacity = 1024;
      while (capacity < respLen * 2 && capacity < maxCapacity) capacity *= 2;

      DeserializationError error;
      bool parsed = false;
      while (capacity <= maxCapacity)
      {
        DynamicJsonDocument doc(capacity);
        error = deserializeJson(doc, response);
        if (!error)
        {
          JsonArray array = doc.as<JsonArray>();
          int arraySize = array.size();
          if (arraySize > 0 ){
            int sent = 0;
            for (JsonObject obj : array) {
              long counter = obj["MissingIdLog"] | obj["MissingIDLog"] | -1;
              if (counter > 0) {
                DtaLogLoss dt;
                dt.Logid = static_cast<int>(counter);
                Serial.println("Counter Loss: " + String(counter));
                if (xQueueSend(LogIdLossQueue, &dt, pdMS_TO_TICKS(50)) != pdPASS) {
                  Serial.println("IdLog node add");
                }
                // avoid starving other tasks
                if ((++sent % 10) == 0) vTaskDelay(pdMS_TO_TICKS(1));
              }
            }
          } else {
            Serial.println("No Data Fined");
          }
          parsed = true;
          break;
        }
        if (error == DeserializationError::NoMemory) {
          Serial.printf("JSON NoMemory with capacity %u, retrying...\n", (unsigned)capacity);
          capacity *= 2;
        } else {
          break;
        }
      }
      if (!parsed) {
        Serial.printf("Error parsing JSON: %s\n", error.c_str());
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
    Serial.print("Khong co ket noi internet");
  }
   // Giải phóng bộ nhớ
  delete msg;
  delete params;
  vTaskDelete(NULL);
}
#endif // API_H