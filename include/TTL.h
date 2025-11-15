#ifndef TTL_H
#define TTL_H
#include <Arduino.h>
#include "structdata.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

inline void sendLogRequest(uint16_t logPosition) {
    // Kiểm tra giới hạn vị trí log
    if (logPosition < 1 || logPosition > 2046) {
        Serial.println("Invalid log position. Must be between 1 and 2046.");
        return;
    }

    // Tách logPosition thành High Byte và Low Byte
    uint8_t highByte = (logPosition >> 8) & 0xFF; // Lấy byte cao
    uint8_t lowByte = logPosition & 0xFF;         // Lấy byte thấp

    // Tính toán checksum
    uint8_t checksum = 0xA5 ^ highByte ^ lowByte;

    // Tạo buffer chứa dữ liệu
    uint8_t buffer[5] = {
        0xC8,        // Byte 1: Lệnh 200
        highByte,    // Byte 2: High Byte của logPosition
        lowByte,     // Byte 3: Low Byte của logPosition
        checksum,    // Byte 4: Checksum
        0xC9         // Byte 5: Lệnh kết thúc 201
    };

    // Gửi toàn bộ buffer
    Serial2.write(buffer, sizeof(buffer));
    // In dữ liệu để kiểm tra
    // Serial.println();
    // Serial.print("Data sent: ");
    // for (int i = 0; i < sizeof(buffer); i++) {
    //     Serial.printf("0x%02X ", buffer[i]);
    // }
    // Serial.println();
}

inline void sendStartupCommand() {
    // Dữ liệu cần gửi
    uint8_t byte1 = 0x7D;  // Byte đầu tiên
    uint8_t control1 = 0x33;  // Control 1
    uint8_t control2 = 0x55;  // Control 2

    // Tính Checksum
    uint8_t checksum = 0xA5 ^ control1 ^ control2;

    uint8_t byte5 = 0x7E;  // Byte cuối cùng

    // Tạo buffer để gửi
    uint8_t buffer[5] = {byte1, control1, control2, checksum, byte5};

    // Gửi toàn bộ buffer cùng lúc
    Serial2.write(buffer, sizeof(buffer));  // Gửi tất cả các byte trong buffer
    Serial2.flush(); // Đợi cho đến khi toàn bộ dữ liệu được truyền đi
    

    // In dữ liệu gửi ra Serial Monitor (dạng hex)
    // Serial.println();
    // Serial.print("Data sent: ");
    // for (int i = 0; i < 5; i++) {
    //     Serial.printf("0x%02X ", buffer[i]);
    // }
    // Serial.println();
}


inline void getLogData(String command, char *&param)
{
  char *tempdata = (char*)pvPortMalloc(LOG_SIZE * sizeof(char));
  if (tempdata == NULL)
  {
    Serial.println("Error: Failed to allocate memory for tempdata");
    return;
  }
  // Serial.println("Lenh Commands: " +command);
  // RS485Serial.flush(); // Đảm bảo bộ đệm truyền trước đó đã được xóa
  Serial2.print(command); //, leng);
  // Kiểm tra xem dữ liệu đã được gửi đi chưa
  Serial2.flush(); // Đợi cho đến khi toàn bộ dữ liệu được truyền đi

  // Kiểm tra lại bộ đệm truyền
  if (Serial2.availableForWrite() < command.length())
  {
    Serial.println("Du lieu goi bi mat");
  }
  byte index = 0;

  vTaskDelay(150 / portTICK_PERIOD_MS); // Thêm độ trễ ngắn trước khi đọc phản hồidf mặc định là 200
  // Giới hạn index để tránh buffer overflow
  while (Serial2.available() > 0 && index < (LOG_SIZE - 1))
  {
    char currentChar = Serial2.read();
    tempdata[index++] = currentChar;
  }
  tempdata[index] = '\0'; // Không tăng index thêm

  // Serial.println(tempdata);
  
  // Giải phóng param cũ nếu có
  if (param != NULL) {
    vPortFree(param);
    param = NULL;
  }

  param = (char *)pvPortMalloc((index + 1) * sizeof(char)); // Cấp phát vùng nhớ mới đủ chứa dữ liệu
  if (param != NULL)
  {
    strcpy(param, tempdata); // Sao chép dữ liệu
    Serial.print("data: "); Serial.println(tempdata);
  }
  else
  {
    Serial.println("Error: Failed to allocate memory for param");
    // Giải phóng tempdata trước khi return
    vPortFree(tempdata);
    return;
  }

  // Giải phóng tempdata sau khi sao chép
  vPortFree(tempdata);
}


// Hàm đọc phản hồi
inline bool readResponse() {
    uint8_t response[4]; // Phản hồi yêu cầu có 4 byte
    unsigned long startTime = millis();

    // Chờ dữ liệu phản hồi trong 2 giây
    while (Serial2.available()>0) {
        if (millis() - startTime > 2500) { // Hết thời gian chờ
            Serial.printf("No response received from ID 99!\n");
            // return false;
            break; // Thoát vòng lặp nếu không nhận được dữ liệu
        }
    }

    // Đọc dữ liệu phản hồi
    for (int i = 0; i < 4; i++) {
        response[i] = Serial2.read();
    }

    // Kiểm tra dữ liệu phản hồi
    Serial.printf("Response from ID 99: ");
    for (int i = 0; i < 4; i++) {
        Serial.printf("0x%02X ", response[i]);
    }
    Serial.println();

    // Phản hồi đúng cấu trúc: 7 + ID Vòi + 'S' hoặc 'E' + 8
    if (response[0] == 7 && response[1] == 99 &&
       (response[2] == 'S' || response[2] == 'E') && response[3] == 8) {
        if (response[2] == 'S') {
            Serial.printf("Command successful for ID 99!\n");
            return true;
        } else if (response[2] == 'E') {
            Serial.printf("Command failed for ID %99!\n");
            return false;
        }
    } else {
        Serial.printf("Invalid response structure from ID 99!\n");
        return false;
    }

    // Đảm bảo tất cả nhánh đều trả về
    return false; // Giá trị mặc định nếu không có nhánh nào khớp
}

// Hàm gửi lệnh SET thời gian
inline void sendSetTimeCommand(TimeSetup *time) {
    uint8_t buffer[10]; // Mảng lưu dữ liệu lệnh
    uint8_t ngay = time->ngay;   // Ngày
    uint8_t thang = time->thang;  // Tháng
    uint8_t nam = time->nam;   // Năm
    uint8_t gio = time->gio;    // Giờ
    uint8_t phut = time->phut;  // Phút
    uint8_t giay = time->giay;  // Giây - Đã sửa lỗi
    uint8_t checksum;

    // Xây dựng lệnh
    buffer[0] = 93;      // Byte 1: Lệnh bắt đầu (93 = 0x5D)
    buffer[1] = 99;   // Byte 2: ID Vòi
    buffer[2] = ngay;    // Byte 3: Ngày
    buffer[3] = thang;   // Byte 4: Tháng
    buffer[4] = nam;     // Byte 5: Năm
    buffer[5] = gio;     // Byte 6: Giờ
    buffer[6] = phut;    // Byte 7: Phút
    buffer[7] = giay;    // Byte 8: Giây

    // Tính checksum (0xA5 XOR với tất cả các byte từ ID Vòi đến Giây)
    checksum = 0xA5;
    for (int i = 1; i < 8; i++) { // XOR từ byte 1 đến byte 7
        checksum ^= buffer[i];
    }
    buffer[8] = checksum; // Byte 9: Checksum
    buffer[9] = 94;       // Byte 10: Lệnh kết thúc (94 = 0x5E)

    // Gửi lệnh qua RS485
    Serial2.write(buffer, 10); // Gửi 10 byte dữ liệu
    Serial2.flush(); // Đợi cho đến khi toàn bộ dữ liệu được truyền đi
    // delayMicroseconds(150); // Thêm độ trễ nhỏ để đảm bảo dữ liệu được gửi đi
    
    // In dữ liệu lệnh để debug
    Serial.printf("Command Sent to ID %d: ", 99);
    for (int i = 0; i < 10; i++) {
        Serial.printf("%d ", buffer[i]);
        // delayMicroseconds(100); // Thêm độ trễ nhỏ giữa các byte để tránh lỗi truyền
    }
    Serial.println();
    readResponse(); // Đọc phản hồi từ thiết bị
}


#endif // TTL_H