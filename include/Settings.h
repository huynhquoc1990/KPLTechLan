#ifndef SETTINGS_H
#define SETTINGS_H
// Thông số của RS485
#define RX_PIN              13//16 // 35 34
#define TX_PIN              5 //17 //32 13
#define ITEM_SIZE_RS485     125
#define RS485BaudRate       9600
#define WIFI_TIMEOUT_MS     20000
#define OUT1                15
#define OUT2                2
#define INPUT1              36
#define FLASH_DATA_FILE "/log.bin" // File lưu dữ liệu log
// File lưu thông tin trên LittleFS
const char* configFile = "/config.txt";
// Định nghĩa tên đăng nhập và mật khẩu cho trang cấu hình
const char* adminUser = "admin";
const char* adminPass = "Qu0c@nh1";


// Thông tin hệ thống mqtt

const char* mqttServer =    "103.57.221.161"; // "tongmcbd.vlxdbdphongit.com"; 103.57.221.161
const int   mqttPort =        1883;                  
const char* mqttUser =      "qhu";
const char* mqttPassword =  "T@nqu0c1";

// Định nghĩa máy chủ NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600 * 7;  // Múi giờ GMT+7
const int   daylightOffset_sec = 3600;


// Cấu hình của Ethernet Lan Ethernet (LAN8720) I/O define:
#define ETH_ADDR        0
#define ETH_POWER_PIN  -1
#define ETH_MDC_PIN    23
#define ETH_MDIO_PIN  18
#define ETH_TYPE      ETH_PHY_LAN8720
#define ETH_CLK_MODE  ETH_CLOCK_GPIO17_OUT

/*
    Các thông tin cần thay đổi khi bàn giao cho khách:
    - TopicMqtt đây là thông tin của thiết bị.
    - wifi_ssid: Wifi dùng để kết nối internet
    - wifi_password: Pass wifi kết nối internet
    - Sửa lại mảng deviceCommands tùy vào trụ cài đặc.
    - Thay đổi số thiết bị kết nối: DeviceNumber mặc định là 5, nhưng thực tế chỉ đọc 3
*/
// char* TopicMqtt =     "QA-T01-V01";
// char* wifi_ssid =     "Quoc Thu";   // Ctyanhthu
// char* wifi_password = "T@nqu0c1"; // atcsoft12345

// Khai báo SSID và Password mặc định
char TopicMqtt[32] = "KPL1"; // SSID mặc định
char wifi_ssid[32] = "KPL_TECH"; // SSID mặc định
char wifi_password[64] = "999999999";     // Mật khẩu mặc định (rỗng)

const char* TopicLogError = "/Error/";
const char* TopicRestart  = "/Restart/";
const char* TopicGetLogIdLoss = "/GetLogIdLoss/";
const char* TopicSendData = "/GetData/";
const char* TopicStatus   = "/GetStatus/";
const char* TopicShift    = "/Shift/";
const char* TopicChange   = "/Change/"; // Topic thay đổi trạng thái bán hàng: bán hàng/ Test bồn/ Lường  

const uint8_t idVoiList[] = {99}; // Thêm các ID vòi khác tại đây


#endif // STRUCTDATA_H