#ifndef SETTINGS_H
#define SETTINGS_H

#include <cstdint>

// Include credentials từ file riêng
#include "Credentials.h"
// Thông số của RS485
#define RX_PIN              35//16-ASR // 35 34  13./ bo ISOlated màu đen: 17 / bo KC868: 13
#define TX_PIN              32 //17-ASR //32 13   5. / bo ISOlated màu đen: 16 / bo KC868: 5
#define ITEM_SIZE_RS485     125
#define RS485BaudRate       9600
#define WIFI_TIMEOUT_MS     20000
#define OUT1                15    // 15 bo A2
#define OUT2                18     // 2  bo A2   , 18 Còi của bo ASR
// #define INPUT1              36
#define RESET_CONFIG_PIN    0     // GPIO0 - nút BOOT trên ESP32, có thể thay đổi      
#define FLASH_DATA_FILE "/log.bin" // File lưu dữ liệu log
// File lưu thông tin trên LittleFS
extern const char* configFile;
// Định nghĩa tên đăng nhập và mật khẩu cho trang cấu hình
extern const char* adminUser;
extern const char* adminPass;


// Thông tin hệ thống mqtt

extern const char* mqttServer; // Server mặc định, sẽ được cập nhật từ API
extern const int   mqttPort;                  
extern const char* mqttUser;
extern const char* mqttPassword;

// Định nghĩa máy chủ NTP
extern const char* ntpServer;
extern const long  gmtOffset_sec;  // Múi giờ GMT+7
extern const int   daylightOffset_sec;


// Cấu hình của Ethernet Lan Ethernet (LAN8720) I/O define:
#define ETH_ADDR        0
#define ETH_POWER_PIN  -1
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN  18
#define ETH_TYPE      ETH_PHY_LAN8720
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT

/*
    Các thông tin cần thay đổi khi bàn giao cho khách:
    - TopicMqtt đây là thông tin của thiết bị.
    - wifi_ssid: Wifi dùng để kết nối internet
    - wifi_password: Pass wifi kết nối internet
    - Sửa lại mảng deviceCommands tùy vào trụ cài đặc.
    - Thay đổi số thiết bị kết nối: DeviceNumber mặc định là 5, nhưng thực tế chỉ đọc 3
*/
// char* TopicMqtt =     "QA-T01-V01";
// char* wifi_ssid =     "Quoc Thu";   // Ctyanhthu
// char* wifi_password = "T@nqu0c1"; // atcsoft12345

// Khai báo SSID và Password mặc định
extern char TopicMqtt[32]; // SSID mặc định
extern char wifi_ssid[32]; // SSID mặc định
extern char wifi_password[64];     // Mật khẩu mặc định (rỗng)

extern const char* TopicLogError;
extern const char* TopicRestart;
extern const char* TopicGetLogIdLoss;
extern const char* TopicSendData;
extern const char* TopicStatus;
extern const char* TopicShift;
extern const char* TopicChange; // Topic thay đổi trạng thái bán hàng: bán hàng/ Test bồn/ Lường  
extern const char* TopicOTA;    // Topic for OTA firmware update
extern const char* TopicUpdatePrice;
extern const char* TopicGetPrice; // Topic to request current prices
extern const char* TopicRequestLog; // Topic to request logs from Flash

extern const uint8_t idVoiList[]; // Thêm các ID vòi khác tại đây


#endif // STRUCTDATA_H
