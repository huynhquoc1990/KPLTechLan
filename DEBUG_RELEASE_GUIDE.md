# 🔧 Debug & Release Build Guide

Hướng dẫn build firmware với 2 chế độ: **Debug** (có log) và **Release** (không log)

---

## 📋 **Tổng quan**

Dự án có **2 environments** trong `platformio.ini`:

| Environment | Description | Serial Logs | Optimization | Use Case |
|------------|-------------|-------------|--------------|----------|
| **debug** | Development | ✅ Full logs | `-O0` (no optimization) | Phát triển & debug |
| **release** | Production (default) | ❌ No debug logs | `-Os` (size optimized) | Sản xuất thực tế |

---

## 🛠️ **Cách build**

### **1️⃣ Build DEBUG (có log đầy đủ)**

```bash
# Build debug version
pio run -e debug

# Upload to ESP32
pio run -e debug -t upload

# Monitor serial output
pio device monitor -e debug
```

**Firmware location:**  
`.pio/build/debug/firmware.bin`

**RAM/Flash usage:**
- RAM: 48,812 bytes (14.9%)
- Flash: 1,095,365 bytes (55.7%)

---

### **2️⃣ Build RELEASE (không log - Production)**

```bash
# Build release version
pio run -e release

# Upload to ESP32
pio run -e release -t upload

# Monitor serial output (chỉ critical errors)
pio device monitor -e release
```

**Firmware location:**  
`.pio/build/release/firmware.bin`

**RAM/Flash usage:**
- RAM: 48,748 bytes (14.9%)
- Flash: **1,060,797 bytes (54.0%)** ← **Tiết kiệm 34KB** so với debug!

---

### **3️⃣ Build mặc định (không cần -e flag)**

Khi chạy `pio run` **KHÔNG có** `-e`, PlatformIO sẽ build **CẢ 2 environments**:

```bash
# Build cả debug VÀ release
pio run

# Kết quả:
# - .pio/build/debug/firmware.bin
# - .pio/build/release/firmware.bin
```

**💡 Tip:** Để build chỉ 1 environment, luôn dùng `-e`:
```bash
pio run -e release        # Chỉ build release
pio run -e debug          # Chỉ build debug
```

---

## 📝 **Cách sử dụng DEBUG macros trong code**

### **Logs bình thường (chỉ debug mode):**

```cpp
// ❌ Cũ (luôn hiện, kể cả production)
Serial.println("This will ALWAYS print");
Serial.printf("Value: %d\n", value);

// ✅ Mới (chỉ hiện khi build debug)
DEBUG_PRINTLN("This only prints in DEBUG mode");
DEBUG_PRINTF("Value: %d\n", value);
DEBUG_PRINT("Inline message without newline");
```

### **Logs critical (luôn hiện, kể cả release mode):**

```cpp
// ✅ Critical errors (luôn hiện)
LOG_ERROR("CRITICAL: WiFi connection failed!");
LOG_ERROR_F("ERROR: Out of memory - %d bytes free\n", freeHeap);
```

---

## 🔍 **So sánh output**

### **Debug Mode:**
```
=== MQTT CALLBACK TRIGGERED ===
Topic: 11223311A/UpdatePrice
[MQTT] UpdatePrice command received - parsing payload...
[MQTT] Current device MST: 11223311A
[MQTT] Payload length: 142 bytes
[MQTT] Raw payload: {"topic":"11223311A","clientid":"11223311A/GetStatus/QA-T01-V01","message":[...]}
[RS485 READ] Price Change Response: 0x07 0x0B 'S'(0x53) 0x08
[RS485 READ] ✓ SUCCESS - DeviceID=11 price updated successfully
[RS485 READ] ✓ Published FinishPrice to QuocAnh/FinishPrice: {...}
Heap: 145234 free, 123456 min free, Temp: 52.3°C
```

### **Release Mode:**
```
WARNING: Low memory!
```

**→ Chỉ có critical errors được hiển thị!**

---

## ⚙️ **Build flags khác biệt**

### **Debug (`-e debug`):**
```ini
-DDEBUG_MODE=1          # Enable debug macros
-DCORE_DEBUG_LEVEL=3    # Full ESP32 debug
-O0                     # No optimization (easier debugging)
-g                      # Include debug symbols
```

### **Release (`-e release`):**
```ini
-DRELEASE_MODE=1        # Disable debug macros
-DCORE_DEBUG_LEVEL=0    # Minimal ESP32 logs
-Os                     # Optimize for size
-DNDEBUG                # Disable assertions
```

---

## 🎯 **Khi nào dùng gì?**

### ✅ **Dùng DEBUG khi:**
- Phát triển tính năng mới
- Tìm lỗi (troubleshooting)
- Test MQTT, RS485, WiFi
- Kiểm tra flow xử lý data

### ✅ **Dùng RELEASE khi:**
- Deploy lên thiết bị khách hàng
- Chạy production 24/7
- Không cần log chi tiết
- Muốn tiết kiệm Flash (OTA nhỏ hơn)

---

## 📊 **Tổng kết ưu điểm**

| Feature | Debug | Release |
|---------|-------|---------|
| **Serial logs** | ✅ Full | ❌ Critical only |
| **Flash size** | 1,095,365 bytes | **1,060,797 bytes** (-34KB) |
| **Startup time** | Slower | **Faster** |
| **Troubleshooting** | ✅ Easy | ❌ Hard |
| **Production ready** | ❌ No | ✅ Yes |

---

## 🚀 **Quick Commands**

```bash
# Debug - Full logs
pio run -e debug -t upload && pio device monitor -e debug

# Release - Production
pio run -e release -t upload && pio device monitor -e release

# Clean all builds
pio run -t clean

# Build both versions
pio run -e debug && pio run -e release
```

---

## 📌 **Lưu ý quan trọng**

1. **KHÔNG commit** file `.bin` vào Git (đã có trong `.gitignore`)
2. **Test đầy đủ** với `debug` trước khi build `release`
3. **Critical logs** (errors, warnings) vẫn hiện ở cả 2 mode
4. **OTA firmware**: Sử dụng file `.pio/build/release/firmware.bin` cho production

---

**Tác giả:** QuocAnh  
**Ngày tạo:** 2025-10-31  
**Version:** 1.0

