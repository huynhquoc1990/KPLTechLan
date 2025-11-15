#include "structdata.h"
#include "TTL.h"

void processAllVoi(TimeSetup *time)
{
    // print time (assuming TimeSetup has hour, minute, second fields)
    Serial.print("Time: ");
    Serial.print(time->gio);
    Serial.print(":");
    Serial.print(time->phut);
    Serial.print(":");
    Serial.println(time->giay);
    Serial.print("Date: ");
    Serial.print(time->ngay);
    Serial.print("/");
    Serial.print(time->thang);
    Serial.print("/");
    Serial.println(time->nam);

    sendSetTimeCommand(time);

    // Dummy implementation
}

void sendLogRequest(unsigned int logPosition)
{
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
}
