#ifndef STRUCTDATA_H
#define STRUCTDATA_H

#include <cstdint>
#include <Arduino.h>
#define MAX_LOGS 5000       // Số log tối đa (cố định)
#define LOG_SIZE 32         // Kích thước mỗi log



struct GetIdLogLoss
{
  char Idvoi[12];
  char Today[12];
  char Request_Code[6];
  char CompanyId[20];
};

// task này nhằm mục đích truyền 2 tham số vào hàm call API để lấy Log data loss và lưu vào
struct TaskParams {
    GetIdLogLoss *msg;
    QueueHandle_t logIdLossQueue;
};


struct DeviceStatus
{
  unsigned long memory;
  unsigned long heap;
  unsigned long stack;
  unsigned long free;
  float temperature;
  unsigned int counterReset;
  char status[50];
  char idDevice[20];
  char ipAddress[20];
};

struct logTagMonTech {
  char id[6];
  char Ma_GD[32];
  char Kieu[25];
  char Ky_Hieu[20];
  char Mau[5];
  char Ngay[10];
  char Gio[10];
  char NhienLieu[30];
  char Tien[10];
  char Dongia[8];
  char Soluong[8];
};

struct CompanyInfo
{
  char CompanyId[20];
  char Mst[18];
  char Product[5];
};

struct Settings
{
  char MqttServer[50];
  uint16_t PortMqtt;
};

struct DtaLogLoss
{
  signed int Logid;
};

struct DeviceGetStatus {
    char Idvoi[12];
    char Request_Code[6];
    byte Status;
};

// Cấu trúc dữ liệu log
struct PumpLog {
  uint8_t send1;         // Byte 1
  uint8_t send2;         // Byte 2
  uint8_t idVoi;         // Byte 3
  uint16_t viTriLogCot;  // Byte 4-5
  uint16_t viTriLogData; // Byte 6-7
  uint16_t maLanBom;     // Byte 8-9
  uint32_t soLitBom;     // Byte 10-13
  uint16_t donGia;       // Byte 14-15
  uint32_t soTotalTong;  // Byte 16-19
  uint32_t soTienBom;    // Byte 20-23
  uint8_t ngay;          // Byte 24
  uint8_t thang;         // Byte 25
  uint8_t nam;           // Byte 26
  uint8_t gio;           // Byte 27
  uint8_t phut;          // Byte 28
  uint8_t giay;          // Byte 29
  uint8_t checksum;      // Byte 30
  uint8_t send3;         // Byte 31
};

struct TimeSetup
{
  uint8_t ngay ;   // Ngày
  uint8_t thang ;  // Tháng
  uint8_t nam ;   // Năm
  uint8_t gio ;    // Giờ
  uint8_t phut ;  // Phút
  uint8_t giay ;  // Giây
};
#endif // STRUCTDATA_H