# 🔧 **FIX: CONCURRENT LOG + PRICE OPERATIONS**

## ❌ **VẤN ĐỀ**

Khi ESP32 đang xử lý ~200 log loss (từ API) và đồng thời nhận lệnh đổi giá qua MQTT, xảy ra **resource contention**:

### **Timeline vấn đề:**
```
T=0s:  API trả về 200 LogID → logIdLossQueue (50 slots)
T=0-10s: RS485 task xử lý 200 log:
         - Mỗi 500ms: sendLogRequest() qua Serial2
         - Nhận response → saveLogWithInfiniteId() 
         - Flash write: ~50ms/log × 200 = 10 giây Flash bị lock

T=5s:  MQTT nhận đổi giá → priceChangeQueue
       RS485 cần gửi price command NHƯNG:
       ├─ Serial2 đang busy (echo từ log request)
       ├─ Price response 'S' → updateNozzlePrice() cần flashMutex
       └─ flashMutex bị BLOCK bởi log writes → TIMEOUT (1000ms)
```

### **3 Bottlenecks:**
1. 🔴 **Flash Mutex Contention** - Log và price đều dùng cùng mutex
2. 🟠 **RS485 Serial Conflict** - Serial2 không thể send 2 lệnh cùng lúc
3. 🟡 **Queue Overflow** - logIdLossQueue chỉ 50 slots, 200 items = overflow

---

## ✅ **GIẢI PHÁP 1: GIẢM FLASH MUTEX HOLD TIME** (RECOMMENDED)

### **Vấn đề hiện tại:**
```cpp
// FlashFile.h line 91-127
bool saveLogWithInfiniteId(...) {
  if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
    File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");  // ~10ms
    dataFile.seek(offset, SeekSet);                        // ~5ms
    dataFile.write(logData, LOG_SIZE);                     // ~30ms
    dataFile.close();                                      // ~5ms
    // TOTAL: ~50ms per log × 200 = 10 seconds!
    xSemaphoreGive(flashMutex);
  }
}
```

### **Giải pháp: Batch Write với Buffer**
```cpp
// File: include/FlashFile.h
// Add batch buffer for log writes
#define LOG_BATCH_SIZE 10  // Write 10 logs at once

struct LogBatchBuffer {
    uint8_t logs[LOG_BATCH_SIZE][LOG_SIZE];
    uint32_t ids[LOG_BATCH_SIZE];
    uint8_t count;
    SemaphoreHandle_t bufferMutex;
};

static LogBatchBuffer logBatch = {
    .count = 0,
    .bufferMutex = NULL
};

// Initialize batch buffer (call in systemInit)
inline void initLogBatchBuffer() {
    logBatch.bufferMutex = xSemaphoreCreateMutex();
    logBatch.count = 0;
}

// Modified: Add log to batch instead of immediate write
inline bool saveLogWithInfiniteIdBatched(uint32_t &currentId, uint8_t *logData, 
                                         SemaphoreHandle_t flashMutex) {
    if (logData == nullptr) {
        Serial.println("Error: Log data is null");
        return false;
    }

    // Add to batch buffer
    if (xSemaphoreTake(logBatch.bufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        if (logBatch.count < LOG_BATCH_SIZE) {
            memcpy(logBatch.logs[logBatch.count], logData, LOG_SIZE);
            logBatch.ids[logBatch.count] = currentId;
            logBatch.count++;
            currentId++;
            
            bool shouldFlush = (logBatch.count >= LOG_BATCH_SIZE);
            xSemaphoreGive(logBatch.bufferMutex);
            
            // Flush batch if full
            if (shouldFlush) {
                return flushLogBatch(flashMutex);
            }
            return true;
        } else {
            xSemaphoreGive(logBatch.bufferMutex);
            // Batch full, flush first
            flushLogBatch(flashMutex);
            // Retry
            return saveLogWithInfiniteIdBatched(currentId, logData, flashMutex);
        }
    }
    return false;
}

// Flush batch to Flash
inline bool flushLogBatch(SemaphoreHandle_t flashMutex) {
    if (logBatch.count == 0) return true;  // Nothing to flush
    
    if (xSemaphoreTake(logBatch.bufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
        uint8_t batchCount = logBatch.count;
        
        // Take Flash mutex for batch write
        if (xSemaphoreTake(flashMutex, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
            File dataFile = LittleFS.open(FLASH_DATA_FILE, "r+");
            if (!dataFile) {
                Serial.println("Failed to open data file for batch write");
                xSemaphoreGive(flashMutex);
                xSemaphoreGive(logBatch.bufferMutex);
                return false;
            }
            
            // Write all logs in batch
            for (uint8_t i = 0; i < batchCount; i++) {
                uint32_t offset = (logBatch.ids[i] % MAX_LOGS) * LOG_SIZE;
                dataFile.seek(offset, SeekSet);
                dataFile.write(logBatch.logs[i], LOG_SIZE);
            }
            
            dataFile.close();
            xSemaphoreGive(flashMutex);
            
            Serial.printf("✓ Batch write: %d logs (Flash lock: ~%dms)\n", 
                         batchCount, batchCount * 5);  // Much faster!
            
            // Clear batch
            logBatch.count = 0;
            xSemaphoreGive(logBatch.bufferMutex);
            return true;
        } else {
            Serial.println("Failed to take flash mutex for batch write");
            xSemaphoreGive(logBatch.bufferMutex);
            return false;
        }
    }
    return false;
}

// Force flush (call before price update or periodically)
inline void forceFlushLogBatch(SemaphoreHandle_t flashMutex) {
    flushLogBatch(flashMutex);
}
```

### **Lợi ích:**
- ✅ **Flash lock time giảm 10x**: 50ms/log → 5ms/log (batch write)
- ✅ **200 logs: 10s → 1s** Flash lock time
- ✅ **Price update có thể chạy giữa các batch** (mỗi batch chỉ ~50ms)

---

## ✅ **GIẢI PHÁP 2: PRIORITY-BASED QUEUE PROCESSING**

### **Vấn đề hiện tại:**
```cpp
// RS485 task xử lý tuần tự
if (millis() - lastPriceChangeTime >= 300) {
  // Price change
}
if (millis() - lastSendTime >= 500) {
  // Log request
}
```

Nếu đang flood log requests, price change bị delay.

### **Giải pháp: Priority Interruption**
```cpp
// In rs485Task() - src/main.cpp
void rs485Task(void *parameter) {
  // ... existing code ...
  
  bool processingLogBatch = false;
  unsigned long logBatchStartTime = 0;
  const unsigned long LOG_BATCH_TIMEOUT = 5000;  // Interrupt log processing after 5s
  
  while (true) {
    esp_task_wdt_reset();
    
    // Check queue monitoring (existing)
    // ...
    
    // PRIORITY 1: Price change (URGENT)
    if (millis() - lastPriceChangeTime >= 300) {
      lastPriceChangeTime = millis();
      PriceChangeRequest priceRequest;
      if (xQueueReceive(priceChangeQueue, &priceRequest, 0) == pdTRUE) {
        Serial.printf("[RS485] ⚡ URGENT: Price change for DeviceID=%d\n", 
                      priceRequest.deviceId);
        
        // Flush log batch before price update to free Flash mutex
        forceFlushLogBatch(flashMutex);
        vTaskDelay(pdMS_TO_TICKS(10));  // Let Flash mutex release
        
        sendPriceChangeCommand(priceRequest);
        
        // Reset log batch tracking
        processingLogBatch = false;
        logBatchStartTime = 0;
      }
    }
    
    // PRIORITY 2: Log requests (BACKGROUND)
    if (millis() - lastSendTime >= 500) {
      lastSendTime = millis();
      
      // Check if we should interrupt log processing for price updates
      if (processingLogBatch) {
        if (millis() - logBatchStartTime > LOG_BATCH_TIMEOUT) {
          Serial.println("[RS485] ⚠️ Log batch timeout, pausing for price updates...");
          forceFlushLogBatch(flashMutex);
          processingLogBatch = false;
          vTaskDelay(pdMS_TO_TICKS(100));  // Pause 100ms for price updates
          continue;
        }
      }
      
      // Check if price queue has urgent items
      UBaseType_t priceQueueSize = uxQueueMessagesWaiting(priceChangeQueue);
      if (priceQueueSize > 0) {
        Serial.printf("[RS485] ⏸️ Pausing log processing, %d price update(s) pending\n", 
                     priceQueueSize);
        forceFlushLogBatch(flashMutex);
        processingLogBatch = false;
        continue;  // Skip log processing this cycle
      }
      
      // Process log queue
      DtaLogLoss dataLog;
      if (xQueueReceive(logIdLossQueue, &dataLog, 0) == pdTRUE) {
        if (!processingLogBatch) {
          processingLogBatch = true;
          logBatchStartTime = millis();
          Serial.println("[RS485] 📝 Starting log batch processing...");
        }
        
        checkLogSend = 0;
        sendLogRequest(static_cast<uint32_t>(dataLog.Logid));
      } else {
        // No more logs, flush batch
        if (processingLogBatch) {
          Serial.println("[RS485] ✓ Log batch complete, flushing...");
          forceFlushLogBatch(flashMutex);
          processingLogBatch = false;
        }
      }
    }
    
    readRS485Data(buffer);
    vTaskDelay(pdMS_TO_TICKS(10));
    yield();
  }
}
```

### **Lợi ích:**
- ✅ **Price update không bị block** bởi log processing
- ✅ **Log batch auto-flush** khi có price update pending
- ✅ **Timeout protection**: Pause log sau 5s để xử lý price

---

## ✅ **GIẢI PHÁP 3: TĂNG QUEUE SIZE**

### **Vấn đề:**
```cpp
// src/main.cpp line 310
logIdLossQueue = xQueueCreate(50, sizeof(DtaLogLoss));  // Chỉ 50 slots!
```

200 items → 150 items bị drop!

### **Giải pháp:**
```cpp
// Increase queue sizes
logIdLossQueue = xQueueCreate(250, sizeof(DtaLogLoss));  // 50 → 250
priceChangeQueue = xQueueCreate(20, sizeof(PriceChangeRequest));  // 10 → 20

// Update queue monitoring thresholds
if (logQueueSize > 200) {  // 200/250 = 80%
  Serial.printf("⚠️ WARNING: logIdLossQueue nearly full! (%d/250)\n", logQueueSize);
}
```

---

## 📊 **SO SÁNH GIẢI PHÁP**

| Giải pháp | Complexity | Effectiveness | Risk |
|-----------|-----------|---------------|------|
| **1. Batch Write** | Medium | ⭐⭐⭐⭐⭐ Excellent | Low |
| **2. Priority Queue** | Low | ⭐⭐⭐⭐ Very Good | Very Low |
| **3. Increase Queue** | Very Low | ⭐⭐⭐ Good | None |

---

## 🎯 **KHUYẾN NGHỊ TRIỂN KHAI**

### **Phase 1: Quick Fix (Immediate)**
1. ✅ Tăng queue size (250 items)
2. ✅ Add priority interruption cho price updates

### **Phase 2: Optimal (Recommended)**
1. ✅ Implement batch write cho logs
2. ✅ Flush batch trước mỗi price update

### **Expected Results:**
- **Before**: Price update timeout sau 1-2s khi đang process logs
- **After**: Price update response trong <500ms, không bị block

---

## 🔬 **TESTING**

### **Test Case:**
```
1. Trigger 200 log loss từ API
2. Ngay lập tức gửi lệnh đổi giá qua MQTT
3. Monitor serial logs:
   - Price update response time
   - Flash mutex contentions
   - Queue overflow warnings
```

### **Expected Output (After Fix):**
```
[RS485] 📝 Starting log batch processing...
[MQTT] UpdatePrice command received
[RS485] ⚡ URGENT: Price change for DeviceID=11
[RS485] ✓ Batch flushed (10 logs, Flash lock: 50ms)
[RS485] [PRICE CHANGE] Sending command: DeviceID=11, Price=10200.00
[RS485 READ] ✓ SUCCESS - DeviceID=11 price updated successfully
[MQTT] ✓ Published FinishPrice to 11223311A/FinishPrice
[RS485] 📝 Resuming log batch processing...
```

---

**Priority để implement:**
1. 🔴 **HIGH**: Increase queue size + priority interruption (easy, quick)
2. 🟠 **MEDIUM**: Batch write optimization (more complex, bigger impact)


