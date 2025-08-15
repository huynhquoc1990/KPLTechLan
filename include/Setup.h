
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <structdata.h>


// Hàm tính checksum
uint8_t calculateChecksum_LogData(const uint8_t* data, size_t length) {
  uint8_t checksum = 0xA5; // Giá trị ban đầu
  for (size_t i = 2; i < 29; i++) { // Xor từ byte ID đến byte Giây
    checksum ^= data[i];
  }
  return checksum;
}

// Hàm gán Buffer Data Log vào PumpLog

void ganLog(byte *buffer, PumpLog &log) {
  log.send1 = buffer[0];
  log.send2 = buffer[1];
  log.idVoi = buffer[2];
  log.viTriLogCot = (buffer[3] << 8) | buffer[4];
  log.viTriLogData = (buffer[5] << 8) | buffer[6];
  log.maLanBom = (buffer[7] << 8) | buffer[8];
  log.soLitBom = (buffer[9] << 24) | (buffer[10] << 16) | (buffer[11] << 8) | buffer[12];
  log.donGia = (buffer[13] << 8) | buffer[14];
  log.soTotalTong = (buffer[15] << 24) | (buffer[16] << 16) | (buffer[17] << 8) | buffer[18];
  log.soTienBom = (buffer[19] << 24) | (buffer[20] << 16) | (buffer[21] << 8) | buffer[22];
  log.ngay = buffer[23];
  log.thang = buffer[24];
  log.nam = buffer[25];
  log.gio = buffer[26];
  log.phut = buffer[27];
  log.giay = buffer[28];
  log.checksum = buffer[29];
  log.send3 = buffer[30];
}

// Hàm chuyển đổi cấu trúc PumpLog sang JSON
String convertPumpLogToJson(const PumpLog &log)
{
  DynamicJsonDocument doc(256); // Use DynamicJsonDocument for v6

  // doc["idVoi"] = log.idVoi;
  // doc["maLanBom"] = log.maLanBom;
  // doc["soLitBom"] = log.soLitBom;
  // doc["donGia"] = log.donGia;
  // doc["soTotalTong"] = log.soTotalTong;
  // doc["soTienBom"] = log.soTienBom;
  // char timestamp[20];
  // sprintf(timestamp, "20%02d-%02d-%02d %02d:%02d:%02d", log.nam, log.thang, log.ngay, log.gio, log.phut, log.giay);
  // doc["timestamp"] = timestamp;

  doc["idVoi"] = log.idVoi;
  doc["posLogCot"] = log.viTriLogCot;
  doc["posLogData"] = log.viTriLogData;
  doc["numsBom"] = log.maLanBom;
  doc["LitBom"] = log.soLitBom;
  doc["donGia"] = log.donGia;
  doc["soTotalTong"] = log.soTotalTong;
  doc["soTienBom"] = log.soTienBom;
  doc["ngay"] = log.ngay;
  doc["thang"] = log.thang;
  doc["nam"] = log.nam;
  doc["gio"] = log.gio;
  doc["phut"] = log.phut;
  doc["giay"] = log.giay;

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}
