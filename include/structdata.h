#include <cstdint>
#include <Arduino.h>

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
  char Mst[12];
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
  char Request_Code[6];
};

struct DeviceGetStatus {
    char Idvoi[12];
    char Request_Code[6];
    byte Status;
};