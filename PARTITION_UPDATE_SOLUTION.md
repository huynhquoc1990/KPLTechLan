# ✅ **GIẢI PHÁP: CẬP NHẬT PARTITION TABLE CHO THIẾT BỊ ĐÃ BÁN**

## 🔴 **VẤN ĐỀ**

**Các bo đã bán đang dùng partition table sai:**
- Default partition (không có `littlefs`) HOẶC
- Old partition (`littlefs` 2MB vượt quá Flash 4MB)
- → Crash `IntegerDivideByZero` khi lưu > 700 logs

**Không thể update partition table qua OTA!**

---

## ✅ **GIẢI PHÁP ĐÃ TRIỂN KHAI**

### **3-Tier Strategy:**

```
┌──────────────────────────────────────────────────┐
│ Tier 1: OTA + Auto Detection (100% devices)     │
│ → Firmware tự phát hiện partition cũ             │
│ → Tắt Flash writes để tránh crash               │
│ → Gửi alert lên server                          │
│ → Hiển thị hướng dẫn qua Serial                 │
└──────────────────────────────────────────────────┘
              ↓
┌──────────────────────────────────────────────────┐
│ Tier 2: Self-Service Update (60-70% users)      │
│ → Server gửi email/SMS với link tool            │
│ → User tự flash theo hướng dẫn                  │
│ → Tool tự động (1-click)                        │
└──────────────────────────────────────────────────┘
              ↓
┌──────────────────────────────────────────────────┐
│ Tier 3: Field Service (5-10% users)             │
│ → Kỹ thuật viên đến tận nơi                     │
│ → Flash hàng loạt nếu có nhiều bo               │
└──────────────────────────────────────────────────┘
```

---

## 📦 **DELIVERABLES**

### **1. Firmware (✅ DONE)**

**File:** `.pio/build/release/firmware.bin`

**Features:**
- ✅ `printPartitionInfo()` - Hiển thị partition info khi boot
- ✅ Auto-detect partition cũ/mới
- ✅ `g_flashSaveEnabled` - Tắt Flash writes nếu partition cũ
- ✅ MQTT alert với field `"oldPartition": true`
- ✅ Serial instructions cho user

**Behavior trên partition cũ:**
```
Boot → Detect partition cũ → Print warning → Disable Flash writes
     → Vẫn nhận logs → Vẫn gửi MQTT → KHÔNG crash
     → Gửi alert lên server: {"oldPartition": true, "warning": "..."}
```

### **2. Partition Table (✅ DONE)**

**File:** `min_spiffs.csv`

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x140000  (1.25MB)
app1,     app,  ota_1,   0x150000, 0x140000 (1.25MB)
eeprom,   data, 0x99,    0x290000, 0x1000
config,   data, spiffs,  0x291000, 0x1F000
littlefs, data, 0x82,    0x2B0000, 0xA0000  (640KB - LittleFS)
spiffs,   data, spiffs,  0x350000, 0xB0000  (704KB)
```

**Capacity:**
- LittleFS: 640 KB → 17,712 logs capacity
- Required: 75 KB for 2046 logs
- Usage: 11.6%
- Headroom: 8.6× requirement

### **3. Flash Tools (✅ DONE)**

**Package:** `kpl-flash-tool/`

Files:
- ✅ `flash-update.bat` (Windows)
- ✅ `flash-update.sh` (Mac/Linux)
- ✅ `README.txt` (Hướng dẫn tiếng Việt)
- ✅ `firmware.bin`
- ✅ `bootloader.bin`
- ✅ `partitions.bin`
- ✅ `boot_app0.bin`

**Size:** ~1.1 MB (dễ download)

### **4. Documentation (✅ DONE)**

- ✅ `DEPLOYMENT_GUIDE.md` - Technical guide
- ✅ `ROLLOUT_PLAN.md` - Timeline & KPIs
- ✅ `PARTITION_UPDATE_SOLUTION.md` - This file
- ✅ `LITTLEFS_DIVIDE_BY_ZERO_FIX.md` - Root cause analysis

---

## 🎯 **QUY TRÌNH CHO USER**

### **Hướng dẫn 5 bước:**

```
BƯỚC 1: Download tool
→ https://kpltech.vn/flash-tool
→ Giải nén file zip

BƯỚC 2: Kết nối USB
→ Cắm cáp USB từ máy tính vào thiết bị

BƯỚC 3: Chạy tool
→ Windows: Double-click flash-update.bat
→ Mac/Linux: Terminal → ./flash-update.sh

BƯỚC 4: Đợi (5 phút)
→ Không ngắt USB
→ Tool tự động erase + flash

BƯỚC 5: Cấu hình WiFi
→ Kết nối WiFi: ESP32-Config-XXXXXX
→ Mở trình duyệt: 192.168.4.1
→ Nhập WiFi + MQTT info
→ Done!
```

---

## 📊 **EXPECTED RESULTS**

### **Sau khi OTA firmware mới:**

**Devices với partition CŨ:**
```
Serial Monitor:
╔═══════════════════════════════════════════════╗
║  ⚠️  THIẾT BỊ CẦN CẬP NHẬT PARTITION TABLE   ║
╚═══════════════════════════════════════════════╝

HƯỚNG DẪN CẬP NHẬT:
1. Tải công cụ: https://kpltech.vn/flash-tool
2. Kết nối thiết bị qua USB
3. Chạy: flash-update.bat
...

⚠️ Thiết bị vẫn hoạt động NHƯNG KHÔNG LƯU LOG
   Logs vẫn được gửi MQTT bình thường.
```

**MQTT Alert:**
```json
{
  "idDevice": "TB001",
  "companyId": "0123456789",
  "oldPartition": true,
  "warning": "OLD_PARTITION_FLASH_REQUIRED",
  "heap": 200000,
  "temperature": 45.2
}
```

**Devices với partition MỚI:**
```
Serial Monitor:
--- VERIFYING PARTITION TABLE ---
Data Partitions:
  - Name: littlefs, Size: 655360 (640.00 KB)
    [✓] CORRECT: LittleFS size is 640KB. OK.
-----------------------------------

✓ Device hoạt động bình thường
✓ Logs được lưu vào Flash
✓ Không crash
```

---

## 🔧 **BACKEND INTEGRATION**

### **API Endpoint nhận alerts:**

```javascript
// POST /api/device/partition-alert
app.post('/api/device/partition-alert', async (req, res) => {
  const { deviceId, macAddress, partitionStatus } = req.body;
  
  // Save to database
  await db.query(`
    INSERT INTO device_partition_status 
    (device_id, mac_address, partition_status, flash_required, last_check)
    VALUES (?, ?, ?, true, NOW())
    ON DUPLICATE KEY UPDATE
      partition_status = ?,
      flash_required = true,
      last_check = NOW()
  `, [deviceId, macAddress, partitionStatus, partitionStatus]);
  
  // Send notification to customer
  const customer = await getCustomerByDevice(deviceId);
  if (customer.email) {
    sendUpdateEmail(customer.email, deviceId);
  }
  if (customer.phone) {
    sendUpdateSMS(customer.phone, deviceId);
  }
  
  res.json({ success: true });
});
```

### **Dashboard Query:**

```sql
-- Devices cần update
SELECT 
  device_id,
  mac_address,
  partition_status,
  last_check,
  flash_completed,
  DATEDIFF(NOW(), last_check) as days_since_check
FROM device_partition_status
WHERE flash_required = true 
  AND flash_completed = false
ORDER BY last_check DESC;
```

---

## 🎓 **LESSONS LEARNED**

### **1. Partition table KHÔNG THỂ OTA**
- Cần erase + flash toàn bộ
- Lần đầu thiết kế phải cẩn thận

### **2. Detection là chìa khóa**
- Firmware tự phát hiện vấn đề
- Không làm phiền user nếu không cần

### **3. Self-service tốt hơn field service**
- Chi phí thấp hơn 10×
- Thời gian nhanh hơn
- Scale tốt hơn

### **4. Testing quan trọng**
- Test partition table trên hardware thật
- Verify build output (`partitions.bin`)
- Check CSV syntax

---

## ✅ **CHECKLIST DEPLOYMENT**

### **Pre-Deployment:**
- [x] Firmware build OK với partition detection
- [x] Partition CSV syntax correct (no comments with commas)
- [x] Flash tools created
- [x] README written
- [ ] Video tutorial recorded
- [ ] Landing page ready
- [ ] Backend API ready
- [ ] Test on 3-5 devices

### **Deployment Week:**
- [ ] OTA 10% devices (soft launch)
- [ ] Monitor for issues
- [ ] OTA 100% devices
- [ ] Send emails/SMS
- [ ] Support hotline active

### **Post-Deployment:**
- [ ] Track update completion
- [ ] Field service for remaining
- [ ] Close tickets
- [ ] Update documentation

---

## 🏆 **EXPECTED OUTCOME**

**Week 2:**
- 100% devices nhận firmware mới
- 80% devices phát hiện partition cũ
- Server có danh sách devices cần update

**Week 3-4:**
- 60-70% users tự flash thành công
- 20-30% users cần support qua hotline
- 5-10% users cần field service

**Week 5:**
- 100% devices có partition mới
- 0 crash reports
- Project complete! 🎉

═══════════════════════════════════════════════════════════

