# 📊 **PUMPLOG STORAGE CAPACITY ANALYSIS**

## 🎯 **YÊU CẦU**

Lưu `PumpLog` với 2 thông tin mới:
1. **Trạng thái MQTT** - Đã gửi MQTT chưa?
2. **Timestamp MQTT** - Ngày giờ gửi MQTT (từ Google time server)

---

## 📐 **STRUCT DESIGN**

### **Original PumpLog (32 bytes):**
```cpp
struct PumpLog {
  uint8_t send1;         // Byte 0
  uint8_t send2;         // Byte 1
  uint8_t idVoi;         // Byte 2: Nozzle ID
  uint16_t viTriLogCot;  // Byte 3-4
  uint16_t viTriLogData; // Byte 5-6
  uint16_t maLanBom;     // Byte 7-8
  uint32_t soLitBom;     // Byte 9-12: Liters × 1000
  uint16_t donGia;       // Byte 13-14: Price
  uint32_t soTotalTong;  // Byte 15-18: Total
  uint32_t soTienBom;    // Byte 19-22: Amount
  uint8_t ngay;          // Byte 23: Day
  uint8_t thang;         // Byte 24: Month
  uint8_t nam;           // Byte 25: Year
  uint8_t gio;           // Byte 26: Hour
  uint8_t phut;          // Byte 27: Minute
  uint8_t giay;          // Byte 28: Second
  uint16_t send3;        // Byte 29
  uint8_t checksum;      // Byte 30
  uint8_t send4;         // Byte 31
};
// Total: 32 bytes
```

### **NEW PumpLog (37 bytes):**
```cpp
struct PumpLog {
  // ... Original 32 bytes ...
  
  // NEW FIELDS (5 bytes):
  uint8_t mqttSent;      // Byte 32: Status
                         //   0 = Not sent yet
                         //   1 = Sent successfully
                         //   2 = Failed to send
  
  time_t mqttSentTime;   // Byte 33-36: Unix timestamp
                         //   Từ Google NTP server
                         //   4 bytes (32-bit Unix time)
};
// Total: 37 bytes
```

---

## 📊 **CAPACITY CALCULATION**

### **Current Configuration:**
```cpp
#define MAX_LOGS 5000       // Số log tối đa
#define LOG_SIZE 37         // Bytes per log (32 + 5)
```

### **Storage Requirements:**
```
Total size = MAX_LOGS × LOG_SIZE
           = 5,000 × 37 bytes
           = 185,000 bytes
           = 185 KB
           = 0.18 MB
```

### **Flash Availability:**
```
ESP32 Flash partition for LittleFS: ~1.5 MB (default)
Used for logs: 185 KB (12.3%)
Available for other data: ~1.3 MB (87.7%)
```

✅ **Kết luận: ĐỦ DƯ GIẤP!**

---

## 📈 **CAPACITY SCENARIOS**

### **Scenario 1: Current (5,000 logs)**
```
Logs: 5,000
Size: 185 KB
Flash usage: 12.3%
Status: ✅ RECOMMENDED
```
**Enough for:**
- ~208 logs/day for 24 days
- ~417 logs/day for 12 days
- ~833 logs/day for 6 days

### **Scenario 2: Double (10,000 logs)**
```
Logs: 10,000
Size: 370 KB
Flash usage: 24.7%
Status: ✅ SAFE
```

### **Scenario 3: Maximum (27,000 logs)**
```
Logs: 27,027
Size: 1,000 KB (1 MB)
Flash usage: 66.7%
Status: ⚠️ POSSIBLE but not recommended
```

---

## 💾 **USAGE EXAMPLES**

### **Example 1: Save log with MQTT status**
```cpp
// In readRS485Data() after receiving pump log
PumpLog log;
ganLog(buffer, log);  // Parse RS485 data (fills bytes 0-31)

// Initialize new fields
log.mqttSent = 0;           // Not sent yet
log.mqttSentTime = 0;       // No timestamp yet

// Save to Flash
if (xQueueSend(mqttQueue, &log, pdMS_TO_TICKS(100)) == pdTRUE) {
  Serial.println("Log queued for MQTT");
}
```

### **Example 2: Update MQTT status after sending**
```cpp
// In sendMQTTData() after successful publish
void sendMQTTData(const PumpLog &log) {
  String jsonData = convertPumpLogToJson(log);
  
  if (mqttClient.publish(fullTopic, jsonData.c_str())) {
    Serial.println("MQTT data sent successfully");
    
    // Update log in Flash with MQTT status
    PumpLog updatedLog = log;
    updatedLog.mqttSent = 1;              // ✅ Sent successfully
    updatedLog.mqttSentTime = time(NULL); // ✅ Timestamp from Google NTP
    
    // TODO: Write back to Flash at current log position
    updateLogMQTTStatus(currentId, updatedLog.mqttSent, updatedLog.mqttSentTime);
  } else {
    // Update with failed status
    updateLogMQTTStatus(currentId, 2, time(NULL));  // Status 2 = Failed
  }
}
```

### **Example 3: Query unsent logs**
```cpp
// Find all logs that haven't been sent to MQTT
void resendFailedLogs() {
  for (uint32_t id = 0; id < currentId; id++) {
    PumpLog log;
    if (readLogWithInfiniteId(currentId, id, &log, flashMutex)) {
      if (log.mqttSent == 0 || log.mqttSent == 2) {
        // Not sent or failed - retry
        Serial.printf("Resending log ID %u\n", id);
        xQueueSend(mqttQueue, &log, 0);
      }
    }
  }
}
```

---

## 🔧 **HELPER FUNCTIONS NEEDED**

### **Function 1: Update MQTT Status**
```cpp
// Add to FlashFile.h
inline bool updateLogMQTTStatus(uint32_t logId, uint8_t mqttSent, 
                                time_t mqttSentTime, SemaphoreHandle_t flashMutex) {
    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
        File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");
        if (!dataFile) {
            Serial.println("Failed to open data file for MQTT status update");
            xSemaphoreGive(flashMutex);
            return false;
        }
        
        // Calculate offset
        uint32_t offset = (logId % MAX_LOGS) * LOG_SIZE;
        
        // Seek to MQTT status fields (byte 32-36)
        dataFile.seek(offset + 32, SeekSet);
        
        // Write status (1 byte)
        dataFile.write(mqttSent);
        
        // Write timestamp (4 bytes)
        dataFile.write((uint8_t*)&mqttSentTime, sizeof(time_t));
        
        dataFile.close();
        xSemaphoreGive(flashMutex);
        
        Serial.printf("Log %u: MQTT status updated (sent=%d, time=%ld)\n", 
                     logId, mqttSent, mqttSentTime);
        return true;
    }
    return false;
}
```

### **Function 2: Get Unsent Logs Count**
```cpp
inline uint32_t getUnsentLogsCount(uint32_t currentId, SemaphoreHandle_t flashMutex) {
    uint32_t count = 0;
    
    if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
        File dataFile = LittleFS.open(FLASH_DATA_FILE, "r");
        if (dataFile) {
            for (uint32_t id = 0; id < currentId && id < MAX_LOGS; id++) {
                uint32_t offset = (id % MAX_LOGS) * LOG_SIZE + 32;  // Offset to mqttSent field
                dataFile.seek(offset, SeekSet);
                
                uint8_t mqttSent = dataFile.read();
                if (mqttSent == 0 || mqttSent == 2) {  // Not sent or failed
                    count++;
                }
            }
            dataFile.close();
        }
        xSemaphoreGive(flashMutex);
    }
    
    return count;
}
```

### **Function 3: Format MQTT Timestamp**
```cpp
inline String formatMQTTTimestamp(time_t timestamp) {
    if (timestamp == 0) {
        return "N/A";
    }
    
    struct tm *timeinfo = localtime(&timestamp);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d-%02d:%02d:%02d",
             timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buffer);
}
```

---

## 🎯 **MIGRATION STRATEGY**

### **Problem: Existing logs don't have new fields**

Old logs (32 bytes) vs New logs (37 bytes) → incompatible!

### **Solution Options:**

#### **Option 1: Clear old logs (RECOMMENDED for simplicity)**
```cpp
// On first boot with new firmware
void migrateLogsIfNeeded() {
    // Check if migration needed (detect old 32-byte format)
    // For simplicity: just clear old logs
    
    Serial.println("Clearing old logs for new format...");
    if (LittleFS.remove(FLASH_DATA_FILE)) {
        Serial.println("Old logs cleared, new format ready");
    }
    currentId = 0;  // Reset counter
}
```

#### **Option 2: Migrate old logs (more complex)**
```cpp
// Read old 32-byte logs, append new 5-byte fields, write back
// Not recommended due to complexity
```

---

## 📊 **SUMMARY TABLE**

| Property | Value | Notes |
|----------|-------|-------|
| **Original struct size** | 32 bytes | RS485 data |
| **New struct size** | 37 bytes | +5 bytes for MQTT tracking |
| **Max logs** | 5,000 | Configurable |
| **Total storage** | 185 KB | 5000 × 37 |
| **Flash available** | ~1.5 MB | LittleFS partition |
| **Flash usage** | 12.3% | Very safe |
| **Capacity** | ✅ **ĐỦ DƯ** | Có thể tăng lên 27K logs nếu cần |

---

## ✅ **RECOMMENDATIONS**

1. ✅ **Use 5,000 logs** (185 KB) - Đủ cho hầu hết use cases
2. ✅ **Clear old logs** on first boot với firmware mới (migration đơn giản)
3. ✅ **Update MQTT status** sau mỗi successful/failed publish
4. ✅ **Implement retry mechanism** cho failed logs (status = 2)
5. ✅ **Add statistics** - Đếm unsent logs định kỳ

---

## 🔧 **IMPLEMENTATION CHECKLIST**

- [x] Update `structdata.h` with new PumpLog struct (37 bytes)
- [x] Update `LOG_SIZE` from 32 to 37
- [ ] Add `updateLogMQTTStatus()` function to FlashFile.h
- [ ] Add `getUnsentLogsCount()` function to FlashFile.h
- [ ] Modify `sendMQTTData()` to update MQTT status after publish
- [ ] Add migration code to clear old logs on first boot
- [ ] Test with real data

---

**Capacity: 5,000 logs × 37 bytes = 185 KB (12.3% Flash usage)**  
**Verdict: ✅ ĐỦ DƯ GIẤP - Có thể lưu đến 27,000 logs nếu cần!**

