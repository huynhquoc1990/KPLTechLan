#ifndef TTL_H
#define TTL_H
#include <Arduino.h>
#include "structdata.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// SETUP PRINTER - ĐẶT NHIÊN LIỆU CHO TỪNG VÒI BƠM
// ============================================================================
// Protocol: Đặt Nhiên liệu cho từng vòi bơm (RON-95 là vòi 1)
// Format: Send(1) + Send(2) + Send('@') + Send('1') + Send(Char n=0-16) + Send(3) + Send(4)
// - Send(1), Send(2): Header decimal 1, 2
// - Send('@'): Command ASCII '@' (64 decimal)
// - Send('1'): Vòi ID - ASCII '1'-'9' hoặc 'A' cho vòi 10
// - Send(Char n=0) đến Send(Char n=16): Tên nhiên liệu (17 ký tự) - GỬI TRỰC TIẾP BYTES
// - Send(3), Send(4): Footer decimal 3, 4
// 
// Ví dụ từ doc (Vòi 1 - RON-95):
// n=0 đến n=16 là: Tên Nhiên Liệu vòi 1= RON-95 là : 1 2 '@' '1' 'R' 'O' 'N' '-' '9' '5' ' ' ' ' '3' 4
// Tổng: 23 bytes
inline void sendSetupPrinterCommandNhienLieu(String nhienlieu, uint8_t address) {
  // Validate input
  if (!nhienlieu || nhienlieu.length() == 0) {
    Serial.println("ERROR: sendSetupPrinterCommandNhienLieu - nhienlieu is empty");
    return;
  }
  
  // Validate address (Vòi ID: 1-10)
  if (address < 1 || address > 10) {
    Serial.printf("ERROR: sendSetupPrinterCommandNhienLieu - invalid address=%d (must be 1-10)\n", address);
    return;
  }
  
  // Build command buffer: Total 23 bytes
  uint8_t buffer[23];
  buffer[0] = 1;        // Send(1) - DECIMAL 1
  buffer[1] = 2;        // Send(2) - DECIMAL 2
  buffer[2] = '@';      // Send('@') - ASCII '@' (64 decimal)
  
  // Send(Char n = address) - Vòi ID (1-10) as ASCII '1'-'9' or 'A'
  if (address <= 9) {
    buffer[3] = '0' + address; // '1'-'9' (ASCII 49-57)
  } else {
    buffer[3] = 'A';            // 'A' (ASCII 65) for vòi 10
  }
  // buffer[3] = '5';            // 'A' (ASCII 65) for vòi 10
  
  // Send(Char n=0) to Send(Char n=16) - Tên nhiên liệu RAW BYTES (17 ký tự)
  // Gửi trực tiếp bytes của string, không convert
  for (int i = 0; i < 17; i++) {
    if (i < nhienlieu.length()) {
      buffer[i + 4] = (uint8_t)nhienlieu.charAt(i); // Raw byte
    } else {
      buffer[i + 4] = ' '; // Pad with space (ASCII 32)
    }
  }
  
  buffer[21] = 3;       // Send(3) - DECIMAL 3
  buffer[22] = 4;       // Send(4) - DECIMAL 4
  
  // Debug log with DECIMAL format
  Serial.printf("[TTL] Set Nhiên Liệu - Vòi %d: %s\n", address, nhienlieu.c_str());
  Serial.print("[TTL] Command (DECIMAL): ");
  for (int i = 0; i < sizeof(buffer); i++) {
    Serial.printf("%c", buffer[i]);
  }
  Serial.println();
  
  // Send command byte-by-byte to ensure stability
  Serial2.write(buffer, sizeof(buffer));
  Serial2.flush();
  
  Serial.println("[TTL] Command sent");
}

// ============================================================================
// SETUP PRINTER - SET TÊN DOANH NGHIỆP VÀ ĐỊA CHỈ
// ============================================================================
// Protocol: Đặt Tên Doanh Nghiệp và Địa chỉ cho máy in
// Format: Send(1) + Send(2) + Send('W') + Send(Char n=0-31) + Send(Char n=32-61) + Send(3) + Send(4)
// - Send(1), Send(2): Header decimal 1, 2
// - Send('W'): Command ASCII 'W' (87 decimal)
// - Send(Char n=0 đến n=31): Tên Doanh Nghiệp (32 ký tự) - GỬI RAW BYTES - VD: CTY A
// - Send(Char n=32 đến n=61): Địa chỉ (30 ký tự) - GỬI RAW BYTES - VD: Số 12 Đường 3122
// - Send(3), Send(4): Footer decimal 3, 4
//
// Ví dụ từ doc:
// n=0 đến n=31 là Tên DN ko đầu; VD: CTY A
// n=32 đến n=61 là Địa chỉ DN ko đầu; VD: Số 12 Đường 3122
// Ví dụ: 1 2 'W' 'C' 'T' 'Y' ' ' 'A' ...(space)... 'S' 'ố' ' ' '1' '2' ' ' 'Đ' 'ư' 'ờ' 'n' 'g' ' ' '3' '1' '2' '2' 3 4
// Tổng: 67 bytes
inline void sendSetupPrinterCommandTenDonVi(String tendonvi, String address) {
  // Validate input
  if (!tendonvi || tendonvi.length() == 0) {
    Serial.println("ERROR: sendSetupPrinterCommandTenDonVi - tendonvi is empty");
    return;
  }
  if (!address || address.length() == 0) {
    Serial.println("ERROR: sendSetupPrinterCommandTenDonVi - address is empty");
    return;
  }
  
  // Build command buffer: Total 67 bytes
  uint8_t buffer[67];
  buffer[0] = 1;        // Send(1) - DECIMAL 1
  buffer[1] = 2;        // Send(2) - DECIMAL 2
  buffer[2] = 'W';      // Send('W') - ASCII 'W' (87 decimal)

  // Send(Char n=0) to Send(Char n=31) - Tên Doanh Nghiệp RAW BYTES (32 ký tự)
  for (int i = 0; i < 32; i++) {
    if (i < tendonvi.length()) {
      buffer[i + 3] = (char)tendonvi.charAt(i); // Raw byte
    } else {
      buffer[i + 3] = ' '; // Pad with space (ASCII 32)
    }
  }
  
  // Send(Char n=32) to Send(Char n=61) - Địa chỉ RAW BYTES (30 ký tự)
  // Buffer position: 35 to 64 (32 chars TenDN + 3 header = start at 35)
  for (int i = 0; i < 30; i++) {
    if (i < address.length()) {
      buffer[i + 35] = (char)address.charAt(i); // Raw byte
    } else {
      buffer[i + 35] = ' '; // Pad with space (ASCII 32)
    }
  }
  
  buffer[65] = 3;       // Send(3) - DECIMAL 3
  buffer[66] = 4;       // Send(4) - DECIMAL 4
  
  // Debug log with DECIMAL format
  Serial.print("[TTL] Command (String): ");
  for (int i = 0; i < sizeof(buffer); i++) {
    // print char ra
    Serial.printf("%c", buffer[i]);
  }
  Serial.println("...");
  
  // Send command byte-by-byte to ensure stability
  Serial2.write(buffer, sizeof(buffer));
  Serial2.flush();
  
  Serial.println("[TTL] Command sent");
}

// ============================================================================
// SETUP PRINTER - ĐẶT MÃ SỐ THUẾ (MST)
// ============================================================================
// Protocol: Đặt MST cho máy in
// Format: Send(1) + Send(2) + Send('#') + Send(Char n=0-17) + Send(3) + Send(4)
// - Send(1), Send(2): Header decimal 1, 2
// - Send('#'): Command ASCII '#' (35 decimal)
// - Send(Char n=0 đến n=17): Mã số thuế (18 ký tự) - GỬI RAW BYTES
// - Send(3), Send(4): Footer decimal 3, 4
// 
// Ví dụ từ doc (MST = 0123456789):
// n=0 đến n=17 là : MST = 0123456789
// Ví dụ: 1 2 '#' '0' '1' '2' '3' '4' '5' '6' '7' '8' '9' ' ' ' ' ' ' ' ' ' ' ' ' 3 4
// Tổng: 23 bytes
inline void sendSetupPrinterCommandMst(String mst) {
  // Validate input
  if (!mst || mst.length() == 0) {
    Serial.println("ERROR: sendSetupPrinterCommandMst - mst is empty");
    return;
  }
  
  // Build command buffer: Total 23 bytes
  uint8_t buffer[23];
  buffer[0] = 1;        // Send(1) - DECIMAL 1
  buffer[1] = 2;        // Send(2) - DECIMAL 2
  buffer[2] = '#';      // Send('#') - ASCII '#' (35 decimal)
  
  // Send(Char n=0) to Send(Char n=17) - MST RAW BYTES (18 ký tự)
  for (int i = 0; i < 18; i++) {
    if (i < mst.length()) {
      buffer[i + 3] = (char)mst.charAt(i); // Raw byte
    } else {
      buffer[i + 3] = ' '; // Pad with space (ASCII 32)
    }
  }
  
  buffer[21] = 3;       // Send(3) - DECIMAL 3
  buffer[22] = 4;       // Send(4) - DECIMAL 4
  
  // Debug log with DECIMAL format
  Serial.printf("[TTL] Set MST: %s\n", mst.c_str());
  Serial.print("[TTL] Command (char): ");
  for (int i = 0; i < sizeof(buffer); i++) {
    Serial.printf("%c", buffer[i]);
  }
  Serial.println();
  
  // Send command byte-by-byte to ensure stability
  Serial2.write(buffer, sizeof(buffer));
  Serial2.flush();
  
  Serial.println("[TTL] Command sent");
}


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