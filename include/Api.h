#ifndef API_H
#define API_H
#include <Arduino.h>
#include "structdata.h"
#include "FlashFile.h"
#include <ETH.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
/// @brief Hàm lấy thông tin từ server và kiểm tra nội dung có thay đổi trong flash hay không? Nếu có lưu mới
/// @param settings
void callAPIGetSettingsMqtt(Settings *settings, SemaphoreHandle_t flashMutex, bool &ethConnected)
{
  // Settings *settings = (Settings *)param;
  // cần đọc data từ file companyInfo.txt lên trước
  Settings settingsInFlash;
  readSettingsInFlash(settingsInFlash, flashMutex);

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
    ethConnected = false;
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

#endif // API_H