# 🚀 **KẾ HOẠCH TRIỂN KHAI CẬP NHẬT PARTITION TABLE**

## 📋 **TIMELINE**

```
Week 1: Preparation & Testing
Week 2: OTA Rollout + Notifications
Week 3-4: User Self-Service Updates
Week 5+: Field Service cho cases đặc biệt
```

---

## 📅 **WEEK 1: CHUẨN BỊ & TESTING**

### **Day 1-2: Chuẩn bị materials**

- [x] Build firmware có partition detection
- [x] Tạo flash tools (Windows/Mac/Linux)
- [x] Tạo README hướng dẫn
- [ ] Quay video hướng dẫn (5 phút)
- [ ] Chụp screenshots từng bước
- [ ] Tạo landing page: https://kpltech.vn/flash-tool

### **Day 3-4: Testing**

Test trên **3-5 thiết bị thật** với các scenarios:
- [ ] Thiết bị có partition cũ (2MB)
- [ ] Thiết bị có partition default (không littlefs)
- [ ] Thiết bị đã update partition mới (640KB)
- [ ] Flash qua Windows
- [ ] Flash qua Mac
- [ ] Flash qua Linux

### **Day 5-7: Backend preparation**

- [ ] Tạo bảng tracking devices trong database:
  ```sql
  CREATE TABLE device_partition_status (
    device_id VARCHAR(50) PRIMARY KEY,
    mac_address VARCHAR(20),
    partition_status VARCHAR(20), -- 'OLD', 'NEW', 'UNKNOWN'
    last_check TIMESTAMP,
    flash_required BOOLEAN,
    flash_completed BOOLEAN,
    flash_date TIMESTAMP
  );
  ```

- [ ] Tạo API endpoint nhận cảnh báo partition:
  ```
  POST /api/device/partition-alert
  {
    "deviceId": "TB001",
    "macAddress": "AA:BB:CC:DD:EE:FF",
    "partitionStatus": "OLD",
    "flashRequired": true
  }
  ```

- [ ] Tạo email/SMS template:
  ```
  Subject: [KPL Tech] Thiết bị {deviceId} cần cập nhật

  Kính gửi Quý khách,

  Thiết bị {deviceId} (MAC: {mac}) của bạn cần cập nhật để hoạt động tốt hơn.

  Cập nhật rất đơn giản:
  1. Tải công cụ: https://kpltech.vn/flash-tool
  2. Làm theo hướng dẫn (5 phút)

  Video hướng dẫn: https://youtu.be/xxxxx

  Hỗ trợ miễn phí: 0xxx-xxx-xxx
  ```

---

## 📅 **WEEK 2: OTA ROLLOUT**

### **Monday: Soft Launch (10% devices)**

- [ ] OTA cho 10% thiết bị (chọn khách hàng thân thiết/gần)
- [ ] Monitor MQTT alerts
- [ ] Gọi điện confirm nhận được notification
- [ ] Thu thập feedback về hướng dẫn

### **Wednesday: Review & Adjust**

- [ ] Phân tích kết quả:
  - Bao nhiêu devices phát hiện partition cũ?
  - Bao nhiêu user đã flash thành công?
  - Có vấn đề gì chưa?
- [ ] Cải thiện hướng dẫn nếu cần
- [ ] Update video/screenshots

### **Friday: Full Rollout (100% devices)**

- [ ] OTA cho tất cả thiết bị còn lại
- [ ] Gửi email/SMS tự động cho devices có partition cũ
- [ ] Setup hotline support

---

## 📅 **WEEK 3-4: SUPPORT & TRACKING**

### **Daily tasks:**

- [ ] Check dashboard: Bao nhiêu devices cần update?
- [ ] Gọi điện nhắc nhở devices chưa update
- [ ] Hỗ trợ user gặp vấn đề qua hotline
- [ ] Update tracking spreadsheet

### **Metrics to track:**

```
Total devices:          100
Partition OK:           20  (20%)
Partition OLD:          80  (80%)
  - Self-service done:  50  (62.5%)
  - Pending:            25  (31.25%)
  - Need field service: 5   (6.25%)
```

---

## 📅 **WEEK 5+: FIELD SERVICE**

### **Cho những trường hợp:**

1. **User không có PC/Mac**
2. **Thiết bị ở vị trí khó tiếp cận**
3. **Khách hàng VIP yêu cầu hỗ trợ tận nơi**

### **Chuẩn bị:**

- [ ] Laptop + USB hub (flash nhiều bo cùng lúc)
- [ ] Flash tool đã cài sẵn
- [ ] Checklist cho kỹ thuật viên
- [ ] Form báo cáo hoàn thành

### **Quy trình field service:**

```
1. Liên hệ khách hàng trước 24h
2. Đến địa điểm vào giờ thấp điểm
3. Flash thiết bị (5 phút/bo)
4. Test MQTT connection
5. Verify logs sending
6. Ký biên bản bàn giao
7. Update tracking database
```

---

## 💰 **CHI PHÍ ƯỚC TÍNH**

### **Self-Service (62.5% cases):**
```
- Development: 40 giờ × $50/h = $2,000 (one-time)
- Hosting: $10/tháng
- Support hotline: 20 giờ × $30/h = $600
Total: $2,610
```

### **Field Service (6.25% cases):**
```
- Travel cost: 5 locations × $50 = $250
- Labor: 5 locations × 2h × $50/h = $500
Total: $750
```

### **Grand Total: $3,360** (cho 100 devices)
**Average: $33.6/device**

---

## 📊 **KPI & SUCCESS METRICS**

### **Week 2 (OTA Rollout):**
- [ ] 100% devices nhận OTA firmware mới
- [ ] 80% devices phát hiện partition cũ
- [ ] 100% devices gửi alert lên server

### **Week 3 (Self-Service):**
- [ ] 40% devices hoàn thành flash
- [ ] < 5% error rate
- [ ] Average support call duration < 10 phút

### **Week 4 (Completion):**
- [ ] 90% devices hoàn thành update
- [ ] < 1% devices có vấn đề kỹ thuật
- [ ] 0 crash reports từ partition issues

### **Week 5+ (Cleanup):**
- [ ] 100% devices hoàn thành update
- [ ] Tất cả devices verify partition OK
- [ ] Close project

---

## 🎯 **ACTION PLAN - BƯỚC TIẾP THEO**

### **Ngay bây giờ:**

1. **Upload flash tool lên server:**
   ```bash
   cd kpl-flash-tool
   zip -r kpl-flash-tool.zip *
   # Upload to: https://kpltech.vn/downloads/kpl-flash-tool.zip
   ```

2. **Tạo landing page đơn giản:**
   - URL: https://kpltech.vn/flash-tool
   - Nội dung: Hướng dẫn + Download link + Video
   - Tracking: Đếm số lượt download

3. **Test OTA trên 1 device:**
   ```bash
   # OTA firmware mới
   # Verify device detect partition cũ
   # Verify MQTT alert được gửi
   ```

### **Tuần tới:**

4. **Setup backend API** nhận partition alerts
5. **Setup email/SMS automation**
6. **Tạo dashboard tracking**
7. **Train support team**

### **2 tuần tới:**

8. **OTA 100% devices**
9. **Monitor & support**
10. **Field service cho cases đặc biệt**

---

## 📞 **SUPPORT RESOURCES**

### **Hotline Script:**

```
"Xin chào, đây là KPL Tech hỗ trợ kỹ thuật.

Tôi giúp gì được cho anh/chị?

[User: Thiết bị báo cần cập nhật]

Vâng, đúng rồi ạ. Thiết bị cần cập nhật 1 lần để hoạt động tốt hơn.
Anh/chị có máy tính Windows hay Mac ạ?

[Hướng dẫn chi tiết theo platform]

Nếu gặp khó khăn, anh/chị có thể đưa thiết bị đến văn phòng
hoặc chúng tôi sẽ cử kỹ thuật viên đến hỗ trợ (miễn phí).

Còn gì thắc mắc không ạ?"
```

### **FAQ:**

**Q: Tại sao cần cập nhật?**
A: Phiên bản cũ có lỗi khi lưu nhiều logs, thiết bị sẽ tự khởi động lại. Bản mới sửa lỗi này.

**Q: Có mất dữ liệu không?**
A: Dữ liệu logs cũ sẽ mất, nhưng logs mới sẽ được đồng bộ tự động từ server.

**Q: Có mất tiền không?**
A: Hoàn toàn miễn phí! Đây là bản cập nhật bảo hành.

**Q: Nếu tôi không có máy tính?**
A: Chúng tôi sẽ cử kỹ thuật viên đến hỗ trợ miễn phí. Vui lòng đặt lịch qua hotline.

**Q: Cập nhật mất bao lâu?**
A: 5-10 phút (tùy tốc độ máy tính).

---

## ✅ **SUMMARY**

**Hiện tại đã có:**
- ✅ Firmware với partition detection
- ✅ Flash tools (Windows/Mac/Linux)
- ✅ README hướng dẫn chi tiết
- ✅ Scripts tự động hóa

**Cần làm tiếp:**
- [ ] Tạo video hướng dẫn
- [ ] Upload tool lên server
- [ ] Setup backend tracking
- [ ] Setup email/SMS automation
- [ ] Test OTA trên 1 device
- [ ] Rollout theo kế hoạch

**Timeline: 4-5 tuần để hoàn thành 100%**

═══════════════════════════════════════════════════════════

