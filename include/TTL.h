#ifndef TTL_H
#define TTL_H
#include <Arduino.h>
void getLogData(String command, char *&param)
{
  char *tempdata = (char *)pvPortMalloc(SIZE_MAX * sizeof(char));
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
  while (Serial2.available() > 0)
  {
    char currentChar = Serial2.read();
    tempdata[index++] = currentChar;
  }
  tempdata[index++] = '\0';

  // Serial.println(tempdata);

  param = (char *)pvPortMalloc((index + 1) * sizeof(char)); // Cấp phát vùng nhớ mới đủ chứa dữ liệu
  if (param != NULL)
  {
    strcpy(param, tempdata); // Sao chép dữ liệu
    // Serial.print("data: "); Serial.println(param);
  }
  else
  {
    Serial.println("Error: Failed to allocate memory for param");
  }

  // Giải phóng tempdata sau khi sao chép
  vPortFree(tempdata);
}

#endif // TTL_H