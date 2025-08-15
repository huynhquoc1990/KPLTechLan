#ifndef FLASHFILE_H
#define FLASHFILE_H
#include <cstdint>
#include "Settings.h"
#include <LittleFS.h>
#include "structdata.h"
#include <freertos/semphr.h>
#include "Inits.h"

// Hàm đọc thông tin từ file
inline String readFileConfig(const char* path) {
  // Không gọi LittleFS.begin() ở đây vì đã được mount ở nơi khác
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Không thể mở file để đọc");
    return "";
  }

  String content = file.readString();
  file.close();
  return content;
}

// Hàm ghi thông tin vào file
inline void writeFileConfig(const char* path, const String& data) {
  // Không gọi LittleFS.begin() ở đây vì đã được mount ở nơi khác
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Không thể mở file để ghi");
    return;
  }

  file.print(data);
  file.close();
}

/// @brief Hàm đọc thông tin lưu dữ liệu trong flash
inline void listFiles(SemaphoreHandle_t flashMutex)
{
  if (xSemaphoreTake(flashMutex, portMAX_DELAY) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");
      xSemaphoreGive(flashMutex);
      return;
    }

    Serial.println("Listing files:");
    File root = LittleFS.open("/");
    if (!root)
    {
      Serial.println("Failed to open root directory");
      xSemaphoreGive(flashMutex);
      return;
    }
    File file = root.openNextFile();
    if (!file)
    {
      Serial.println("Failed to open file");
      root.close();
      LittleFS.end();
      xSemaphoreGive(flashMutex);
      return;
    }
    while (file)
    {
      Serial.print("File: ");
      Serial.print(file.name());
      Serial.print(" - Size: ");
      Serial.println(file.size());
      file = root.openNextFile();
    }
    file.close();
    root.close();
    LittleFS.end();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
}

// Lưu log vào file với ID vô cùng
inline bool saveLogWithInfiniteId(uint32_t &currentId, uint8_t *logData, SemaphoreHandle_t flashMutex)
{
    // kIỂM TRA TÍNH HỢP LỆ CỦA LOGDATA
    if (logData == nullptr) {
        Serial.println("Error: Log data is null");
        return false;
    }

    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
    {
        File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");
        if (!dataFile)
        {
            Serial.println("Failed to open data file");
            return false;
        }

        // Tính offset của log (theo ID)
        uint32_t offset = (currentId % MAX_LOGS) * LOG_SIZE;

        // Ghi log tại vị trí offset
        dataFile.seek(offset, SeekSet);
        dataFile.write(logData, LOG_SIZE);
        dataFile.close();

        Serial.print("Log saved with ID: ");
        Serial.println(currentId);
        // Tăng ID
        currentId++;
        xSemaphoreGive(flashMutex);
        return true;
    }else {
        Serial.println("Error: Failed to take semaphore for writing");
        return false;
    }

}

// Khi đọc log, bạn cần chuyển ID vô hạn về vị trí thực tế (với modulo):
inline bool readLogWithInfiniteId(uint32_t currentId, uint32_t id, uint8_t *&logData, SemaphoreHandle_t flashMutex)
{
    if (id >= currentId || id < (currentId > MAX_LOGS ? currentId - MAX_LOGS : 0))
    {
        Serial.println("Error: Log ID out of range");
        return false;
    }

    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
    {
        File dataFile = LittleFS.open(FLASH_DATA_FILE, FILE_WRITE);
        if (!dataFile)
        {
            Serial.println("Failed to open data file");
            return false;
        }

        // Tính offset từ ID
        uint32_t offset = (id % MAX_LOGS) * LOG_SIZE;

        // Đọc log tại offset
        dataFile.seek(offset, SeekSet);
        size_t readBytes = dataFile.read(logData, LOG_SIZE);

        dataFile.close();
        xSemaphoreGive(flashMutex);

        if (readBytes != LOG_SIZE) {
            Serial.println("Error: Failed to read complete log data");
            return false;
        }

        Serial.printf("Log read successfully for ID: %u\n", id);
        return true;
    }else {
        Serial.println("Error: Failed to take semaphore for reading");
        return false;
    }
}

// Hàm này sẽ trả về vị trí log tương ứng trong flash dựa trên ID vô hạn:
inline uint32_t convertIdToFlashIndex(uint32_t id) {
  return id % MAX_LOGS; // Chuyển về khoảng 0-4999
}

inline void clearAllLogs(uint32_t &currentId, SemaphoreHandle_t flashMutex) {
    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
        if (LittleFS.exists(FLASH_DATA_FILE)) {
            if (LittleFS.remove(FLASH_DATA_FILE)) {
                Serial.println("All logs cleared successfully");
            } else {
                Serial.println("Error: Failed to clear logs");
            }
        } else {
            Serial.println("No log file found to clear");
        }

        currentId = 0; // Reset ID về 0
        xSemaphoreGive(flashMutex);
    } else {
        Serial.println("Error: Failed to take semaphore for clearing logs");
    }
}

inline uint32_t initializeCurrentId(SemaphoreHandle_t flashMutex) {
    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {

        if (!LittleFS.begin()) {
            Serial.println("Error: File system not mounted.");
            return -1;  // Trả về lỗi nếu LittleFS chưa được gắn kết
        }

        File dataFile = LittleFS.open(FLASH_DATA_FILE, FILE_READ);
        if (!dataFile) {
            Serial.println("Log file not found, initializing currentId to 0");
            // Kiểm tra nếu tệp chưa tồn tại
            if (!LittleFS.exists(FLASH_DATA_FILE)) {
                Serial.println("File log.bin not found, creating it...");

                // Mở tệp ở chế độ ghi để tạo
                File file = LittleFS.open(FLASH_DATA_FILE, FILE_WRITE);
                if (!file) {
                    Serial.println("Error: Unable to create log file.");
                    xSemaphoreGive(flashMutex);  // Thả semaphore
                    return -1;
                }
                file.close();  // Đóng tệp sau khi tạo
                Serial.println("Log file created successfully.");
            }
            xSemaphoreGive(flashMutex);  // Thả semaphore trước khi trả về
            // Trả về ID khởi tạo là 0
            return 0;
        }

        // Tính toán số lượng log dựa trên kích thước file
        uint32_t fileSize = dataFile.size();
        dataFile.close();

        uint32_t calculatedId = fileSize / LOG_SIZE; // Số lượng log đã lưu
        Serial.printf("Current ID initialized to: %u (from file size: %u bytes)\n", calculatedId, fileSize);
        xSemaphoreGive(flashMutex);  // Thả semaphore
        return calculatedId;
    } else {
        Serial.println("Error: Failed to take semaphore for clearing logs");
        return -1;
    }
}

/// @brief Format thông tin Flash
inline bool initLittleFS()
{
    // Attempt to initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS. Attempting to format...");
        if (!LittleFS.format()) {
            Serial.println("Failed to format LittleFS.");
            return false;
        }
        Serial.println("LittleFS formatted successfully.");
        if (!LittleFS.begin()) {
            Serial.println("Still failed to mount LittleFS.");
            return false;
        }
    }
    Serial.println("LittleFS mounted successfully.");
    
    // Test filesystem by trying to create a test file
    File testFile = LittleFS.open("/test.txt", "w");
    if (!testFile) {
        Serial.println("LittleFS mount successful but cannot create files. Reformatting...");
        LittleFS.end();
        if (!LittleFS.format()) {
            Serial.println("Failed to reformat LittleFS.");
            return false;
        }
        if (!LittleFS.begin()) {
            Serial.println("Failed to remount LittleFS after reformat.");
            return false;
        }
        testFile = LittleFS.open("/test.txt", "w");
        if (!testFile) {
            Serial.println("Still cannot create files after reformat.");
            return false;
        }
    }
    testFile.println("LittleFS test");
    testFile.close();
    LittleFS.remove("/test.txt");
    Serial.println("LittleFS filesystem test passed.");
    return true;
}

/// @brief Lưu nội dung settings file vào bộ nhớ Flash
/// @param data
inline void saveFileSettingsToFlash(const String &data, SemaphoreHandle_t flashMutex)
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");
    //   strcpy(deviceStatus->status, "Failed to mount file system");
      xSemaphoreGive(flashMutex);
      return;
    }

    File file = LittleFS.open("/settings.txt", FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to open file for writing");
    //   strcpy(deviceStatus->status, "Failed to open file for writing");
      file.close();
      xSemaphoreGive(flashMutex);
      return;
    }
    if (file.print(data.c_str()))
    { // Sử dụng printf để lưu dữ liệu
      Serial.println("File written successfully");
    }
    else
    {
      Serial.println("Write failed");
    //   strcpy(deviceStatus->status, "Write failed");
    }
    file.close();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
    // strcpy(deviceStatus->status, "Failed to take flash mutex");
  }
}

/// @brief Đọc thông tin thiết lập trong bộ nhớ Falash
/// @param settings
inline void readSettingsInFlash(Settings &settings, SemaphoreHandle_t flashMutex)
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to mount file system");
      xSemaphoreGive(flashMutex);
    //   strcpy(deviceStatus->status, "Failed to mount file system");
      return;
    }
    if (!LittleFS.exists("/settings.txt"))
    {
      Serial.println("File settings.txt not found");
    //   strcpy(deviceStatus->status, "File settings.txt not found");
      File file = LittleFS.open("/settings.txt", FILE_WRITE);
      file.close();
    }
    File file = LittleFS.open("/settings.txt", FILE_READ);
    if (!file)
    {
      Serial.println("Failed to open file settings.txt for reading");
    //   strcpy(deviceStatus->status, "Failed read settings.txt");
      file.close();
      xSemaphoreGive(flashMutex);
      return;
    }

    String line = file.readStringUntil('\n');
    file.close();
    xSemaphoreGive(flashMutex);
    convertSettingsFromHex(line, settings);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
//   strcpy(deviceStatus->status, "Failed to take flash mutex");
}

/// @brief Đọc các thông tin thiết lập trong bộ nhớ Flash
inline void readFlashSettings(SemaphoreHandle_t flashMutex, DeviceStatus &deviceStatus, unsigned long &counterReset)
{
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE)
  {
    if (!LittleFS.begin())
    {
      Serial.println("Failed to initialize LittleFS");
      xSemaphoreGive(flashMutex);
      strcpy(deviceStatus.status, "Failed to initialize LittleFS");
      return;
    }
    if (!LittleFS.exists("/counter.bin"))
    {
      Serial.println("File counter.bin not found");
      File file = LittleFS.open("/counter.bin", FILE_WRITE);
      file.close();
      strcpy(deviceStatus.status, "File counter.bin not found");
    }

    File file = LittleFS.open("/counter.bin", "r");
    if (!file)
    {
      Serial.println("Failed to read from flash");
      strcpy(deviceStatus.status, "Failed to read from flash");
      xSemaphoreGive(flashMutex);
      return;
    }

    file.readBytes((char *)&counterReset, sizeof(counterReset));
    file.close();
    counterReset = counterReset + 1;
    file = LittleFS.open("/counter.bin", "w");
    if (!file)
    {
      Serial.println("Failed to write to flash");
      strcpy(deviceStatus.status, "Failed to write to flash");
      xSemaphoreGive(flashMutex);
      return;
    }

    file.write((const uint8_t *)&counterReset, sizeof(counterReset));
    file.close();
    Serial.println(counterReset);
    LittleFS.end();
    xSemaphoreGive(flashMutex);
  }
  else
  {
    Serial.println("Failed to take flash mutex");
  }
  strcpy(deviceStatus.status, "Failed to take flash mutex");
}

#endif // STRUCTDATA_H