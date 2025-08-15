#ifndef WEBSERVICE_H
#define WEBSERVICE_H

#include <Settings.h>
#include <FlashFile.h>
#include <ESPAsyncWebServer.h>
// Trang HTML đăng nhập
extern const char* loginPage;

// Trang trả lại thông báo
extern const char* result;

// Trang HTML cấu hình
extern const char* configPage;

//Trang hiển thị trạng thái của internet
extern const char* checkInternet;

//Trang hiển thị trạng thái của internet
extern const char* checkInternetNoConnect;

//Trang HTML cấu hình
extern const char* configPage1;

//Trang HTML báo lỗi đăng nhập
extern const char* loginFail;

//Trang HTML báo lỗi logout
extern const char* logout;

//Trang HTML server fail
extern const char* serverFail;

void printHello();
void checkConnect(
    bool eth_connected,
    bool wifi_connected,
    const char *&result,
    const char *&color
);
#endif