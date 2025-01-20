#ifndef API_H
#define API_H
#include <Arduino.h>
#include "structdata.h"
#include "FlashFile.h"
#include <ETH.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/queue.h>

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
            saveFileSettingsToFlash(hexString, flashMutex);
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

        //   strcpy(companyInfo->CompanyId, company->CompanyId);
        //   strcpy(companyInfo->Mst, company->Mst);
        //   strcpy(companyInfo->Product, company->Product);

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
    String url = "http://103.57.221.161:5002/get-managerment/giaodich/Idloss";
    http.begin(url);
    // Headers
    http.addHeader("Content-Type", "application/json");
    // JSON data
    JsonDocument doc;
    doc["idvoi"] = msg->Idvoi;
    doc["today"] = msg->Today;
    doc["request_code"] = msg->Request_Code;
    doc["company"] = msg->CompanyId;

    String json;
    serializeJson(doc, json);

    Serial.print("json:" + json +"\n");
    // Send the POST request
    int httpResponseCode = http.POST(json.c_str());

    if (httpResponseCode > 0 && httpResponseCode == 200)
    {
      String response = http.getString();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, response);
      if (!error)
      {
        // Get the size of the array
        JsonArray array = doc.as<JsonArray>();
        int arraySize = array.size();
        // Serial.printf("Len: %d\n", arraySize);
        // Loop through the JSON array and store the values in the integer array
        if (arraySize > 0 ){
          for (int i = 0; i < arraySize; i++)
          {
            DtaLogLoss dt;
            signed int counter = array[i]["MissingIdLog"];
            // dt.Logid = value;
            Serial.println("Counter Loss: " + String(counter));
            dt.Logid = counter;
            // Serial.print("gapCounter: "); Serial.print(gapCounter);Serial.print("  idLog: "); Serial.print(idLog);Serial.print("  Counter: "); Serial.println(counter);
            /* tính toán lại Logid dựa vào counter và chênh lệch giauwx counter và IdLog trước đó*/
            if (xQueueSend(LogIdLossQueue, &dt, portMAX_DELAY) != pdPASS)
            {
                Serial.println("IdLog node add");
            }
          }
        }else {
          Serial.println("No Data Fined");
        }
      }
      else
      {
        Serial.printf("Error parsing JSON: %s", error.c_str());
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