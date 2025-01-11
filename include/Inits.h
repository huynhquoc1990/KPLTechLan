#ifndef INITS_H
#define INITS_H
#include <Arduino.h>
#include "structdata.h"
#include <ArduinoJson.h>
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

/// @brief Hàm dùng để tách chuổi đọc được, lấy giá trị từ chuổi đọc được, giống hàm Mid trong excel
/// @param data : Chuổi cần đọc
/// @param start: Vị trí bắt đầu cắt chuổi
/// @param end : Vi trí kết thúc chuổi
/// @return
String getValue(String data, String start, String end)
{
  int startIndex = data.indexOf(start);
  if (startIndex == -1)
    return ""; // Không tìm thấy từ khóa bắt đầu
  startIndex += start.length();
  int endIndex = data.indexOf(end, startIndex);
  if (endIndex == -1)
    return ""; // Không tìm thấy từ khóa kết thúc
  String result = data.substring(startIndex, endIndex);
  result.trim();
  return result;
}

/// @brief Lọc các ký tự lạ trong serial
/// @param input
/// @return
String clearnData(String input)
{
  String result = "";
  for (int i = 0; i < input.length(); i++)
  {
    char c = input[i];
    if (c >= 32 && c <= 126)
    { // ASCII từ 32 đến 126 là ký tự in được
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
void parsePayload_IdLogStatus(byte *payload, unsigned int length, DeviceGetStatus *deviceGetStatus)
{
  // Tạo một buffer cho payload để xử lý với ArduinoJson
  char json[length + 1];
  memcpy(json, payload, length);
  json[length] = '\0'; // Đảm bảo chuỗi kết thúc

  // Khởi tạo đối tượng JSON document
  JsonDocument doc;

  // Phân tích chuỗi JSON
  DeserializationError error = deserializeJson(doc, json);
  if (error)
  {
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

#endif // STRUCTDATA_H