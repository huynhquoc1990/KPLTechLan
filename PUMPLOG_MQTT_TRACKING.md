# ✅ **PUMPLOG MQTT TRACKING - IMPLEMENTATION**

## 🎯 **LOGIC FLOW**

### **Yêu cầu:**
1. ✅ Log ID = `viTriLogData` (1-2046) từ TTL
2. ✅ **Gửi MQTT trước** → Lưu Flash sau
3. ✅ Nếu MQTT **thành công** → `mqttSent = 1` + timestamp → Flash
4. ✅ Nếu MQTT **failed** → `mqttSent = 0` + timestamp → Flash

---

## 📊 **CAPACITY**

### **Configuration:**
```cpp
#define MAX_LOGS 2046       // Theo TTL (1-2046)
#define LOG_SIZE 37         // 32 bytes gốc + 5 bytes tracking
```

### **Storage:**
```
Total = 2,046 logs × 37 bytes = 75,702 bytes = 75.7 KB
ESP32 Flash LittleFS = ~1.5 MB
Usage = 75.7 KB / 1.5 MB = 5.0%
```

✅ **RẤT NHỎ!** - Chỉ dùng 5% Flash

---

## 🔄 **IMPLEMENTATION FLOW**

### **Step 1: RS485 nhận log**
```cpp
// In readRS485Data() - src/main.cpp line 1776
if (firstByte == 1 && Serial2.available() >= LOG_SIZE) {
  byte buffer[LOG_SIZE];
  Serial2.readBytes(buffer, LOG_SIZE);
  
  // Validate checksum
  if (calculatedChecksum == receivedChecksum) {
    PumpLog log;
    ganLog(buffer, log);  // Parse RS485 data
    
    // log.viTriLogData = 1-2046 (từ TTL)
    // log.mqttSent = 0 (initialized)
    // log.mqttSentTime = 0 (initialized)
    
    // Queue for MQTT
    xQueueSend(mqttQueue, &log, pdMS_TO_TICKS(100));
  }
}
```

### **Step 2: MQTT Task xử lý**
```cpp
// In mqttTask() - src/main.cpp
void mqttTask(void *parameter) {
  while (true) {
    if (mqttClient.connected()) {
      PumpLog log;
      if (xQueueReceive(mqttQueue, &log, pdMS_TO_TICKS(100)) == pdTRUE) {
        sendMQTTData(log);  // Try send MQTT + Save to Flash
      }
    }
  }
}
```

### **Step 3: Send MQTT + Save Flash**
```cpp
// In sendMQTTData() - src/main.cpp line 1523
void sendMQTTData(const PumpLog &log) {
  bool mqttSuccess = false;
  
  // Try send MQTT (3 retries)
  for (int retry = 0; retry < 3; retry++) {
    if (mqttClient.publish(fullTopic, jsonData.c_str())) {
      mqttSuccess = true;
      break;
    }
  }
  
  // Update log với MQTT status
  PumpLog updatedLog = log;
  updatedLog.mqttSentTime = time(NULL);  // Từ Google NTP
  
  if (mqttSuccess) {
    updatedLog.mqttSent = 1;  // ✅ Success
  } else {
    updatedLog.mqttSent = 0;  // ❌ Failed
  }
  
  // Save to Flash tại vị trí viTriLogData
  uint32_t offset = (updatedLog.viTriLogData - 1) * LOG_SIZE;  // viTriLogData is 1-based
  
  File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");
  dataFile.seek(offset, SeekSet);
  dataFile.write((const uint8_t*)&updatedLog, LOG_SIZE);  // Write 37 bytes
  dataFile.close();
  
  Serial.printf("💾 Log %d saved (status=%d, time=%ld)\n", 
                updatedLog.viTriLogData, updatedLog.mqttSent, updatedLog.mqttSentTime);
}
```

---

## 📐 **STRUCT DESIGN**

```cpp
struct PumpLog {
  // Original fields từ RS485 (32 bytes - Byte 0-31)
  uint8_t send1;         // Byte 0
  uint8_t send2;         // Byte 1
  uint8_t idVoi;         // Byte 2: Nozzle ID
  uint16_t viTriLogCot;  // Byte 3-4
  uint16_t viTriLogData; // Byte 5-6: Log ID (1-2046) ← KEY FIELD
  uint16_t maLanBom;     // Byte 7-8
  uint32_t soLitBom;     // Byte 9-12
  uint16_t donGia;       // Byte 13-14
  uint32_t soTotalTong;  // Byte 15-18
  uint32_t soTienBom;    // Byte 19-22
  uint8_t ngay;          // Byte 23
  uint8_t thang;         // Byte 24
  uint8_t nam;           // Byte 25
  uint8_t gio;           // Byte 26
  uint8_t phut;          // Byte 27
  uint8_t giay;          // Byte 28
  uint16_t send3;        // Byte 29
  uint8_t checksum;      // Byte 30
  uint8_t send4;         // Byte 31
  
  // NEW: MQTT tracking (5 bytes - Byte 32-36)
  uint8_t mqttSent;      // Byte 32: 0=failed, 1=success
  time_t mqttSentTime;   // Byte 33-36: Unix timestamp (Google NTP)
};
// Total: 37 bytes
```

---

## 📊 **FLASH LAYOUT**

```
File: /log.bin
Size: 2046 × 37 = 75,702 bytes (75.7 KB)

Position mapping:
viTriLogData = 1    → Offset = 0 × 37 = 0
viTriLogData = 2    → Offset = 1 × 37 = 37
viTriLogData = 3    → Offset = 2 × 37 = 74
...
viTriLogData = 2046 → Offset = 2045 × 37 = 75,665
```

**Formula:** `offset = (viTriLogData - 1) × 37`

---

## 🔍 **USAGE EXAMPLES**

### **Example 1: Read log from Flash**
```cpp
// Read log at position viTriLogData = 100
void readLogFromFlash(uint16_t viTriLogData) {
  if (viTriLogData < 1 || viTriLogData > MAX_LOGS) {
    Serial.println("Invalid viTriLogData");
    return;
  }
  
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    File dataFile = LittleFS.open(FLASH_DATA_FILE, "r");
    if (dataFile) {
      uint32_t offset = (viTriLogData - 1) * LOG_SIZE;
      dataFile.seek(offset, SeekSet);
      
      PumpLog log;
      dataFile.read((uint8_t*)&log, LOG_SIZE);
      dataFile.close();
      
      Serial.printf("Log %d: MQTT sent=%d, time=%ld\n", 
                    viTriLogData, log.mqttSent, log.mqttSentTime);
      
      // Format timestamp
      if (log.mqttSentTime > 0) {
        struct tm *timeinfo = localtime(&log.mqttSentTime);
        Serial.printf("  Timestamp: %02d/%02d/%04d-%02d:%02d:%02d\n",
                     timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
                     timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
      }
    }
    xSemaphoreGive(flashMutex);
  }
}
```

### **Example 2: Find failed logs**
```cpp
// Scan Flash để tìm logs chưa gửi thành công
void findFailedLogs() {
  uint32_t failedCount = 0;
  
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    File dataFile = LittleFS.open(FLASH_DATA_FILE, "r");
    if (dataFile) {
      for (uint16_t id = 1; id <= MAX_LOGS; id++) {
        uint32_t offset = (id - 1) * LOG_SIZE;
        dataFile.seek(offset, SeekSet);
        
        // Read only MQTT status field (byte 32)
        dataFile.seek(offset + 32, SeekSet);
        uint8_t mqttSent = dataFile.read();
        
        if (mqttSent == 0) {  // Failed
          failedCount++;
          Serial.printf("Log %d: MQTT failed\n", id);
        }
      }
      dataFile.close();
      Serial.printf("Total failed logs: %u\n", failedCount);
    }
    xSemaphoreGive(flashMutex);
  }
}
```

### **Example 3: Retry failed logs**
```cpp
// Resend logs that failed MQTT
void retryFailedLogs() {
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    File dataFile = LittleFS.open(FLASH_DATA_FILE, "r");
    if (dataFile) {
      for (uint16_t id = 1; id <= MAX_LOGS; id++) {
        uint32_t offset = (id - 1) * LOG_SIZE;
        dataFile.seek(offset, SeekSet);
        
        PumpLog log;
        dataFile.read((uint8_t*)&log, LOG_SIZE);
        
        if (log.mqttSent == 0 && log.viTriLogData == id) {
          // Failed log found, retry
          Serial.printf("Retrying log %d...\n", id);
          xQueueSend(mqttQueue, &log, 0);  // Re-queue for MQTT
        }
      }
      dataFile.close();
    }
    xSemaphoreGive(flashMutex);
  }
}
```

---

## 📁 **FILES MODIFIED**

### **1. `include/structdata.h`** ✅
- `MAX_LOGS`: 5000 → 2046 (theo TTL)
- `LOG_SIZE`: 32 → 37 bytes
- `PumpLog`: Added `mqttSent` + `mqttSentTime`

### **2. `include/Setup.h`** ✅
- `ganLog()`: Initialize `mqttSent = 0` và `mqttSentTime = 0`

### **3. `src/main.cpp`** ✅
- `sendMQTTData()`: 
  - Try MQTT (3 retries)
  - Set `mqttSent` + `mqttSentTime`
  - Save to Flash at `viTriLogData` position

---

## 📊 **BUILD STATUS**

```
✅ Build: SUCCESS
✅ RAM:   16.0% (52,488 bytes) - No change
✅ Flash: 54.5% (1,070,873 bytes) - +840 bytes for new logic
✅ Linter: No errors
✅ Storage: 75.7 KB (5% of Flash)
```

---

## 🎯 **SUMMARY**

| Property | Value | Notes |
|----------|-------|-------|
| **Log ID range** | 1-2046 | Từ viTriLogData (TTL) |
| **Log size** | 37 bytes | 32 gốc + 5 tracking |
| **Total storage** | 75.7 KB | 2046 × 37 |
| **Flash usage** | 5.0% | Rất nhỏ |
| **MQTT flow** | Try MQTT → Save Flash | Status + timestamp |
| **Timestamp source** | Google NTP | `time(NULL)` |

---

## ✅ **LOGIC ĐÚNG THEO YÊU CẦU**

1. ✅ **Log ID = viTriLogData** (1-2046) từ TTL
2. ✅ **Gửi MQTT trước** - Try 3 lần với retry
3. ✅ **Nếu thành công**: `mqttSent = 1`, save timestamp → Flash
4. ✅ **Nếu failed**: `mqttSent = 0`, vẫn save timestamp → Flash
5. ✅ **Flash position** = `(viTriLogData - 1) × 37`
6. ✅ **Capacity**: 2046 logs = 75.7 KB (chỉ 5% Flash)

---

**✅ Implementation complete và tested!** 🚀

