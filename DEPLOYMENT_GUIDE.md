# 📘 **HƯỚNG DẪN TRIỂN KHAI CẬP NHẬT PARTITION TABLE CHO THIẾT BỊ ĐÃ BÁN**

## 🎯 **MỤC TIÊU**

Cập nhật partition table cho các thiết bị đã bán để sửa lỗi `IntegerDivideByZero` khi lưu > 700 logs.

---

## 📊 **TỔNG QUAN GIẢI PHÁP**

### **Chiến lược: OTA + Self-Service Update**

```
┌─────────────────────────────────────────────────┐
│  BƯỚC 1: OTA Firmware Mới (có detection)       │
│  → Thiết bị tự phát hiện partition cũ          │
│  → Gửi cảnh báo lên server/MQTT                 │
│  → Vẫn hoạt động (nhưng không lưu Flash)       │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  BƯỚC 2: Server gửi thông báo cho khách hàng   │
│  → Email/SMS với link download tool             │
│  → Hướng dẫn chi tiết bằng hình ảnh            │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  BƯỚC 3: Khách hàng tự flash (hoặc gọi hỗ trợ) │
│  → Download tool từ link                        │
│  → Chạy flash-update.bat/.sh                   │
│  → 5 phút hoàn tất                             │
└─────────────────────────────────────────────────┘
                      ↓
┌─────────────────────────────────────────────────┐
│  BƯỚC 4: Thiết bị hoạt động bình thường         │
│  → Partition mới (LittleFS 640KB)              │
│  → Không còn crash khi lưu logs                │
└─────────────────────────────────────────────────┘
```

---

## 🔧 **BƯỚC 1: CHUẨN BỊ FIRMWARE VÀ TOOLS**

### **1.1. Build Firmware Mới**

Firmware đã có sẵn các tính năng:
- ✅ Tự động detect partition cũ/mới
- ✅ Hiển thị hướng dẫn nếu partition cũ
- ✅ Tắt Flash writes để tránh crash
- ✅ Gửi cảnh báo lên MQTT

```bash
cd /Users/quocanhgas/Program-QHU/KPLTechLan
~/.platformio/penv/bin/pio run -e release
```

**Output:**
- `.pio/build/release/firmware.bin`
- `.pio/build/release/bootloader.bin`
- `.pio/build/release/partitions.bin`

### **1.2. Tạo Flash Tool Package**

Tạo thư mục `kpl-flash-tool` với cấu trúc:

```
kpl-flash-tool/
├── README.txt                  (Hướng dẫn tiếng Việt)
├── esptool.exe                 (Windows tool)
├── esptool                     (Mac/Linux tool)
├── flash-update.bat            (Windows script)
├── flash-update.sh             (Mac/Linux script)
├── bootloader.bin
├── partitions.bin
├── boot_app0.bin
├── firmware.bin
└── images/                     (Screenshots hướng dẫn)
    ├── step1-connect-usb.jpg
    ├── step2-run-tool.jpg
    ├── step3-config-wifi.jpg
    └── step4-complete.jpg
```

**Tạo package:**

<function_calls>
<invoke name="run_terminal_cmd">
<parameter name="command">cd /Users/quocanhgas/Program-QHU/KPLTechLan && mkdir -p kpl-flash-tool && cp .pio/build/release/firmware.bin kpl-flash-tool/ && cp .pio/build/release/bootloader.bin kpl-flash-tool/ && cp .pio/build/release/partitions.bin kpl-flash-tool/ && cp flash-update.sh kpl-flash-tool/ && cp flash-update.bat kpl-flash-tool/ && ls -lh kpl-flash-tool/
