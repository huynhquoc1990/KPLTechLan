# 💾 Nozzle Price Storage - Flash Memory

Hướng dẫn hệ thống lưu giá vòi bơm vào Flash Memory

---

## 📋 **Tổng quan**

Mỗi khi giá được cập nhật thành công qua MQTT và nhận response `'S'` từ KPL device, giá mới sẽ được **lưu vào Flash** tự động.

### **Mục đích:**
- ✅ Giữ giá sau khi mất điện/reset
- ✅ Khôi phục giá khi khởi động lại
- ✅ Kiểm tra checksum đảm bảo tính toàn vẹn dữ liệu
- ✅ Hỗ trợ 10 vòi bơm (Nozzle 11-20)

---

## 🏗️ **Cấu trúc dữ liệu**

### **1. Struct `NozzlePrices`:**

```cpp
struct NozzlePrices {
    float prices[10];      // prices[0] = Nozzle 11, ..., prices[9] = Nozzle 20
    uint32_t lastUpdate;   // Timestamp of last update
    uint8_t checksum;      // Data integrity check
};
```

### **2. File location:**
```
/nozzle_prices.dat (LittleFS)
```

**Size:** 
- `10 floats * 4 bytes = 40 bytes`
- `1 uint32_t * 4 bytes = 4 bytes`
- `1 uint8_t * 1 byte = 1 byte`
- **Total: 45 bytes**

---

## 🔄 **Flow hoạt động**

```
1. ESP32 Boot
   ↓
2. systemInit() → loadNozzlePrices()
   ↓
3. Load prices from /nozzle_prices.dat
   ↓
4. Verify checksum → OK ✓
   ↓
5. Print all prices to Serial
   ↓
6. Prices ready in RAM (nozzlePrices)

═══════════════════════════════════════

7. MQTT receives UpdatePrice
   ↓
8. Send command to RS485 (TTL)
   ↓
9. KPL device response: [07][ID][S][08]
   ↓
10. Parse status = 'S' (Success)
   ↓
11. updateNozzlePrice() → Save to Flash
   ↓
12. Update checksum & timestamp
   ↓
13. Write to /nozzle_prices.dat
   ↓
14. ✓ Price saved successfully!
```

---

## 🔧 **API Functions**

### **1. Load prices from Flash:**
```cpp
bool loadNozzlePrices(NozzlePrices &prices, SemaphoreHandle_t flashMutex);
```

**Usage:**
```cpp
NozzlePrices prices;
if (loadNozzlePrices(prices, flashMutex)) {
    Serial.println("✓ Prices loaded successfully");
} else {
    Serial.println("✗ No saved prices, using defaults");
}
```

---

### **2. Save all prices to Flash:**
```cpp
bool saveNozzlePrices(const NozzlePrices &prices, SemaphoreHandle_t flashMutex);
```

---

### **3. Update single nozzle price:**
```cpp
bool updateNozzlePrice(uint8_t nozzleId, float newPrice, 
                       NozzlePrices &prices, SemaphoreHandle_t flashMutex);
```

**Usage:**
```cpp
// Update Nozzle 13 to 10200 VND
if (updateNozzlePrice(13, 10200.0f, nozzlePrices, flashMutex)) {
    Serial.println("✓ Nozzle 13 price saved to Flash");
}
```

---

### **4. Get single nozzle price:**
```cpp
float getNozzlePrice(uint8_t nozzleId, const NozzlePrices &prices);
```

**Usage:**
```cpp
float price = getNozzlePrice(13, nozzlePrices);
Serial.printf("Nozzle 13 price: %.2f VND\n", price);
```

---

### **5. Print all prices:**
```cpp
void printNozzlePrices(const NozzlePrices &prices);
```

**Output:**
```
╔═══════════════════════════════════════╗
║       NOZZLE PRICES (Flash)           ║
╠═══════════════════════════════════════╣
║ Nozzle 11:   10000.00 VND          ║
║ Nozzle 12:   12500.00 VND          ║
║ Nozzle 13:   10200.00 VND          ║
║ Nozzle 14:   15000.00 VND          ║
║ Nozzle 15:   13500.00 VND          ║
║ Nozzle 16:       0.00 VND          ║
║ Nozzle 17:       0.00 VND          ║
║ Nozzle 18:       0.00 VND          ║
║ Nozzle 19:       0.00 VND          ║
║ Nozzle 20:       0.00 VND          ║
║ Last Update:  123456789 ms           ║
║ Checksum: 0xAB                       ║
╚═══════════════════════════════════════╝
```

---

## 🔐 **Checksum Algorithm**

### **Công thức:**
```cpp
uint8_t checksum = 0x5A; // Magic value
for (each byte in prices + lastUpdate) {
    checksum ^= byte;
}
```

### **Validation:**
```cpp
if (calculated_checksum != saved_checksum) {
    Serial.println("✗ Data corrupted!");
    return false;
}
```

---

## 📊 **Example: Full Workflow**

### **Scenario: Update Nozzle 13 price to 10200 VND**

#### **Step 1: MQTT Publish**
```json
Topic: 11223311A/UpdatePrice

Payload:
{
  "message": [{
    "item": {
      "IDChiNhanh": "11223311A",
      "Nozzorle": "13",
      "UnitPrice": 10200
    }
  }]
}
```

#### **Step 2: Serial Log (Debug mode)**
```
[MQTT] UpdatePrice: Received 1 price entries
[MQTT] ✓ Queued price change: Nozzorle=13, Price=10200.00

[RS485] Processing price change request for DeviceID=13
[PRICE CHANGE] Sending command: DeviceID=13, Price=10200.00
[PRICE CHANGE] ✓ Command sent to KPL
[PRICE CHANGE] Data (HEX): 0x09 0x0D 0x30 0x31 0x30 0x32 0x30 0x30 [CS] 0x0A

[RS485 READ] Price Change Response: 0x07 0x0D 'S'(0x53) 0x08
[RS485 READ] ✓ SUCCESS - DeviceID=13 price updated successfully

[FLASH] ✓ Nozzle 13 price updated: 10200.00 VND
[FLASH] ✓ Nozzle prices saved successfully

[RS485 READ] ✓ Published FinishPrice to QuocAnh/FinishPrice
```

#### **Step 3: Flash Storage**
```
File: /nozzle_prices.dat
Size: 45 bytes

Content (after update):
prices[2] = 10200.0f    // Nozzle 13 (index = 13 - 11 = 2)
lastUpdate = 123456789
checksum = 0xAB         // Recalculated
```

#### **Step 4: After reboot**
```
[FLASH] ✓ Nozzle prices loaded successfully

╔═══════════════════════════════════════╗
║       NOZZLE PRICES (Flash)           ║
╠═══════════════════════════════════════╣
║ Nozzle 13:   10200.00 VND          ║  ← Preserved!
╚═══════════════════════════════════════╝
```

---

## 🚨 **Error Handling**

### **1. File not found (First boot):**
```
[FLASH] Nozzle prices file not found, initializing defaults...
→ Create new file with all prices = 0.0
→ Save to Flash
```

### **2. Checksum mismatch:**
```
[FLASH] ✗ Nozzle prices checksum mismatch: 0xAB != 0xCD
→ Data corrupted, ignore file
→ Initialize with defaults
```

### **3. Invalid file size:**
```
[FLASH] ✗ Invalid nozzle prices file size: 30 bytes (expected 45)
→ File corrupted, ignore
```

### **4. Flash mutex timeout:**
```
[FLASH] ✗ Failed to take flash mutex for saving prices
→ Retry on next update
```

---

## 🔍 **Mapping: Nozzle ID → Array Index**

| Nozzle ID | Array Index | Formula |
|-----------|-------------|---------|
| 11 | 0 | `index = nozzleId - 11` |
| 12 | 1 | `index = nozzleId - 11` |
| 13 | 2 | `index = nozzleId - 11` |
| 14 | 3 | `index = nozzleId - 11` |
| 15 | 4 | `index = nozzleId - 11` |
| 16 | 5 | `index = nozzleId - 11` |
| 17 | 6 | `index = nozzleId - 11` |
| 18 | 7 | `index = nozzleId - 11` |
| 19 | 8 | `index = nozzleId - 11` |
| 20 | 9 | `index = nozzleId - 11` |

---

## 🎯 **Testing**

### **Test 1: First boot (no saved prices)**
```
Expected:
- [FLASH] Nozzle prices file not found
- Initialize all prices to 0.0
- Save to Flash
- Print prices: all 0.00 VND
```

### **Test 2: Update price via MQTT**
```
1. Send UpdatePrice for Nozzle 13 = 10200
2. Wait for RS485 response 'S'
3. Check Serial: [FLASH] ✓ Nozzle 13 price updated
4. Restart ESP32
5. Check Serial: Nozzle 13 should show 10200.00
```

### **Test 3: Multiple updates**
```
1. Update Nozzle 11 = 10000
2. Update Nozzle 15 = 12500
3. Update Nozzle 20 = 15000
4. Restart ESP32
5. All 3 prices should be preserved
```

---

## 📌 **Performance**

| Operation | Time | Notes |
|-----------|------|-------|
| Load from Flash | ~10ms | On boot only |
| Save to Flash | ~50ms | After each successful update |
| Checksum calc | <1ms | Very fast |
| Mutex wait | <1000ms | Timeout if Flash busy |

---

## 💡 **Best Practices**

1. ✅ **Always check return value:**
   ```cpp
   if (!updateNozzlePrice(id, price, prices, mutex)) {
       Serial.println("Failed to save, will retry");
   }
   ```

2. ✅ **Print prices after boot:**
   ```cpp
   printNozzlePrices(nozzlePrices); // Debug
   ```

3. ✅ **Don't save too frequently:**
   - Only save when KPL confirms with 'S'
   - Flash has ~100,000 write cycles limit

4. ✅ **Use mutex for thread safety:**
   - Always pass `flashMutex` to protect concurrent access

---

## 🔧 **Maintenance**

### **Clear saved prices (if needed):**
```cpp
// In code:
LittleFS.remove(NOZZLE_PRICES_FILE);
ESP.restart();

// Or via Serial command (if implemented)
```

### **Backup prices:**
```cpp
// Read current prices
printNozzlePrices(nozzlePrices);

// Copy output to safe place
// Restore manually via MQTT if needed
```

---

**Tác giả:** QuocAnh  
**Ngày tạo:** 2025-10-31  
**Version:** 1.0

