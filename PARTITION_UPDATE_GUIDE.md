# 📘 **HƯỚNG DẪN CẬP NHẬT PARTITION TABLE**

## ⚠️ **QUAN TRỌNG**

**Partition table KHÔNG THỂ update qua OTA!**

Lý do:
- Partition table ở offset `0x8000` (bootloader metadata)
- OTA chỉ có thể update firmware (`app0`/`app1`)
- Update partition table sai → **BRICK DEVICE** (không boot được)

---

## 🔧 **3 PHƯƠNG ÁN CẬP NHẬT**

### **Phương án 1: FLASH QUA USB (RECOMMENDED)** ⭐

**Ưu điểm:**
- ✅ An toàn nhất
- ✅ Nhanh nhất (2-3 phút/device)
- ✅ Có thể backup data trước khi flash

**Nhược điểm:**
- ❌ Cần USB access (thiết bị phải ở hiện trường)
- ❌ Cần PC/laptop có Python + PlatformIO

---

#### **Bước 1: Chuẩn bị**

```bash
# Install esptool (nếu chưa có)
pip3 install esptool

# Build firmware mới
cd /Users/quocanhgas/Program-QHU/KPLTechLan
pio run -e release
```

---

#### **Bước 2: Chạy script tự động**

**Sử dụng script đã tạo:**

```bash
# Linux/Mac
./flash-new-partition.sh /dev/ttyUSB0

# Windows
./flash-new-partition.sh COM3
```

**Script sẽ tự động:**
1. Build firmware
2. Hỏi có muốn backup LittleFS không
3. Erase toàn bộ Flash
4. Flash firmware mới + partition table mới

---

#### **Bước 3: Manual (nếu script lỗi)**

```bash
# Step 1: Erase toàn bộ Flash
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash

# Step 2: Flash firmware mới (bao gồm partition table)
pio run -t upload -e release --upload-port /dev/ttyUSB0

# Hoặc dùng esptool trực tiếp:
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size detect \
  0x1000 .pio/build/release/bootloader.bin \
  0x8000 .pio/build/release/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/release/firmware.bin
```

---

#### **Bước 4: Reconfigure Device**

Sau khi flash xong:

1. **Device reboot → Config Portal**
   - SSID: `ESP32-Config-XXXXXX`
   - IP: `192.168.4.1`

2. **Kết nối WiFi**
   - Nhập SSID/Password
   - Nhập MQTT server

3. **Kiểm tra Status**
   ```
   Serial Monitor → Should see:
   - LittleFS mounted: 640 KB
   - System OK
   - MQTT connected
   ```

4. **Restore Data qua MQTT**
   - Gửi lại giá (`UpdatePrice`)
   - Device sẽ tự nhận logs mới

---

### **Phương án 2: FACTORY RESET MODE (Cho thiết bị xa)**

**Ý tưởng:**
- Thêm "Factory Reset" mode vào firmware
- User nhấn nút → Device tự erase + reboot
- Cần USB flash 1 lần để apply partition mới

---

#### **Implementation:**

**Thêm vào `main.cpp`:**

```cpp
void checkFactoryResetButton()
{
  // Check if reset button held for 10 seconds
  if (digitalRead(RESET_CONFIG_PIN) == LOW)
  {
    static unsigned long resetPressTime = 0;
    if (resetPressTime == 0) {
      resetPressTime = millis();
    }
    
    if (millis() - resetPressTime > 10000) // 10 seconds
    {
      Serial.println("⚠️⚠️⚠️ FACTORY RESET TRIGGERED ⚠️⚠️⚠️");
      Serial.println("This device will be prepared for partition update");
      Serial.println("After restart, device MUST be flashed via USB!");
      
      // Erase LittleFS
      LittleFS.format();
      
      // Erase NVS (WiFi config)
      nvs_flash_erase();
      nvs_flash_init();
      
      Serial.println("✓ Data erased");
      Serial.println("⚠️ Device will now restart");
      Serial.println("⚠️ CONNECT TO USB AND RUN: ./flash-new-partition.sh");
      
      delay(5000);
      ESP.restart();
    }
  }
  else
  {
    resetPressTime = 0;
  }
}
```

**Workflow:**
1. User nhấn giữ nút RESET 10 giây
2. Device xóa data + restart
3. Technician đến hiện trường với laptop
4. Flash qua USB: `./flash-new-partition.sh /dev/ttyUSB0`

---

### **Phương án 3: STAGED ROLLOUT (Cho nhiều thiết bị)**

**Chiến lược:**
- Update từng đợt (batch), không update hết cùng lúc
- Mỗi đợt 10-20 devices

---

#### **Bước 1: Lập Danh Sách Devices**

```json
{
  "batch_1": [
    {"device_id": "TB001", "location": "Cột 1", "mac": "AA:BB:CC:DD:EE:01"},
    {"device_id": "TB002", "location": "Cột 2", "mac": "AA:BB:CC:DD:EE:02"}
  ],
  "batch_2": [
    {"device_id": "TB011", "location": "Cột 11", "mac": "AA:BB:CC:DD:EE:11"},
    {"device_id": "TB012", "location": "Cột 12", "mac": "AA:BB:CC:DD:EE:12"}
  ]
}
```

---

#### **Bước 2: Schedule Downtime**

**Kế hoạch:**
```
Week 1: Batch 1 (10 devices)
- Monday 2AM-4AM: Flash 5 devices
- Tuesday 2AM-4AM: Flash 5 devices

Week 2: Batch 2 (10 devices)
- Monday 2AM-4AM: Flash 5 devices
- Tuesday 2AM-4AM: Flash 5 devices

...continue until all devices updated
```

---

#### **Bước 3: Preparation Checklist**

**Trước khi flash mỗi device:**

- [ ] Backup logs qua MQTT (`RequestLog`)
- [ ] Backup prices (`GetPrice`)
- [ ] Record current firmware version
- [ ] Record uptime/statistics
- [ ] Notify server: device going offline

**Sau khi flash:**

- [ ] Verify boot OK
- [ ] Verify LittleFS size = 640 KB
- [ ] Restore WiFi config
- [ ] Restore prices via MQTT
- [ ] Verify logs syncing
- [ ] Monitor for 1 hour
- [ ] Notify server: device back online

---

## 🔍 **KIỂM TRA PARTITION TABLE HIỆN TẠI**

### **Trên Device (qua Serial Monitor):**

```cpp
void printPartitionInfo()
{
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, 
                                                     ESP_PARTITION_SUBTYPE_ANY, 
                                                     "littlefs");
  if (it != NULL) {
    const esp_partition_t* partition = esp_partition_get(it);
    Serial.printf("LittleFS Partition:\n");
    Serial.printf("  Address: 0x%X\n", partition->address);
    Serial.printf("  Size: %u bytes (%.1f KB)\n", partition->size, partition->size / 1024.0);
    
    if (partition->size == 0x200000) {
      Serial.println("⚠️ OLD PARTITION (2 MB) - NEEDS UPDATE!");
    } else if (partition->size == 0xA0000) {
      Serial.println("✓ NEW PARTITION (640 KB) - OK!");
    }
    
    esp_partition_iterator_release(it);
  }
}
```

---

### **Qua esptool (không cần firmware):**

```bash
# Read partition table
esptool.py --port /dev/ttyUSB0 read_flash 0x8000 0xC00 partition.bin

# Parse partition table
gen_esp32part.py partition.bin

# Expected output for NEW partition:
# littlefs, data, littlefs, 0x2B0000, 0xA0000
#           ^^^^             ^^^^^^^  ^^^^^^
#           Type             Offset   Size (640KB)

# OLD partition shows:
# littlefs, data, littlefs, 0x2B1000, 0x200000 ← BAD!
```

---

## 📊 **TRACKING SPREADSHEET**

**Tạo Google Sheet để track progress:**

| Device ID | Location | MAC Address | Current Partition | Status | Flash Date | Technician | Notes |
|-----------|----------|-------------|-------------------|--------|------------|------------|-------|
| TB001 | Cột 1 | AA:BB:...:01 | OLD (2MB) | ⏳ Pending | - | - | - |
| TB002 | Cột 2 | AA:BB:...:02 | OLD (2MB) | ✓ Done | 2024-01-15 | John | OK |
| TB003 | Cột 3 | AA:BB:...:03 | NEW (640KB) | ✓ Done | 2024-01-15 | John | OK |

---

## ⚠️ **RỦI RO VÀ GIẢM THIỂU**

### **Rủi ro 1: Brick Device**

**Nguyên nhân:**
- Flash bị gián đoạn (mất điện, rút USB)
- Partition table bị corrupt

**Giảm thiểu:**
- ✅ Dùng UPS/battery backup khi flash
- ✅ Dùng USB cable chất lượng tốt
- ✅ Test trên 1-2 devices trước khi flash hàng loạt

**Recovery:**
```bash
# Nếu device brick, flash lại:
esptool.py --chip esp32 --port /dev/ttyUSB0 erase_flash
pio run -t upload -e release --upload-port /dev/ttyUSB0
```

---

### **Rủi ro 2: Mất Dữ Liệu**

**Dữ liệu sẽ mất:**
- ❌ WiFi config (phải setup lại)
- ❌ Logs trong Flash (~1200 logs)
- ❌ Prices trong Flash

**Giảm thiểu:**
- ✅ Backup logs trước qua MQTT (`RequestLog`)
- ✅ Backup prices qua MQTT (`GetPrice`)
- ✅ Server tự động gửi lại prices sau khi device reconnect

---

### **Rủi ro 3: Downtime**

**Downtime dự kiến:**
- Flash via USB: **2-3 phút**
- Reconfigure WiFi: **1-2 phút**
- Total: **5 phút/device**

**Giảm thiểu:**
- ✅ Flash vào giờ ít giao dịch (2AM-4AM)
- ✅ Thông báo downtime trước cho users
- ✅ Chuẩn bị sẵn config (SSID/Password/MQTT)

---

## 🎯 **DECISION MATRIX**

| Scenario | Recommended Method |
|----------|-------------------|
| **1-10 devices, có access hiện trường** | **Phương án 1: USB Flash** |
| **>50 devices, spread across locations** | **Phương án 3: Staged Rollout** |
| **Devices xa, khó access** | **Phương án 2: Factory Reset → Schedule USB flash** |
| **Emergency (device brick)** | **Phương án 1: USB Flash (immediate)** |

---

## 📝 **CHECKLIST: TRƯỚC KHI FLASH**

### **Preparation:**
- [ ] Build firmware mới: `pio run -e release`
- [ ] Test trên 1 device trước
- [ ] Backup logs qua MQTT
- [ ] Backup prices qua MQTT
- [ ] Chuẩn bị USB cable
- [ ] Chuẩn bị laptop có Python + esptool
- [ ] Thông báo downtime

### **During Flash:**
- [ ] Connect USB securely
- [ ] Run `./flash-new-partition.sh /dev/ttyUSB0`
- [ ] Wait for "Flash successful" message
- [ ] Don't disconnect until complete

### **After Flash:**
- [ ] Verify device boots OK
- [ ] Connect to Config Portal
- [ ] Configure WiFi
- [ ] Verify MQTT connection
- [ ] Restore prices via MQTT
- [ ] Monitor for 1 hour
- [ ] Mark device as "Updated" in spreadsheet

---

## 🚀 **QUICK START: Flash 1 Device**

**5-minute procedure:**

```bash
# 1. Build firmware (1 min)
cd /Users/quocanhgas/Program-QHU/KPLTechLan
pio run -e release

# 2. Connect device via USB

# 3. Flash (2 min)
./flash-new-partition.sh /dev/ttyUSB0

# 4. Reconfigure (2 min)
# - Connect to ESP32-Config-XXXXXX
# - Open 192.168.4.1
# - Enter WiFi credentials
# - Device reconnects to MQTT

# 5. Done! ✓
```

---

## 📞 **SUPPORT**

**Nếu gặp vấn đề:**

1. **Device không boot sau flash:**
   ```bash
   esptool.py --port /dev/ttyUSB0 erase_flash
   pio run -t upload -e release --upload-port /dev/ttyUSB0
   ```

2. **LittleFS mount failed:**
   - Check partition table: `gen_esp32part.py partition.bin`
   - Verify size: Should be `0xA0000` (640 KB)

3. **Device brick (không detect được):**
   - Hold BOOT button
   - Press RESET button
   - Release RESET (still hold BOOT)
   - Run flash command
   - Release BOOT

---

## ✅ **SUMMARY**

### **TL;DR:**
- ❌ **KHÔNG THỂ OTA partition table**
- ✅ **PHẢI flash qua USB**
- ⏱️ **5 phút/device**
- 🔧 **Dùng script: `./flash-new-partition.sh`**

### **Next Steps:**
1. Test trên 1-2 devices
2. Lập danh sách devices cần update
3. Schedule downtime
4. Flash từng batch
5. Monitor và verify

**Partition table mới ổn định, tránh được lỗi divide by zero!** 🚀

