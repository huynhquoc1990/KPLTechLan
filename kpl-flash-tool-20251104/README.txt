═══════════════════════════════════════════════════════════
  KPL TECH - HƯỚNG DẪN CẬP NHẬT THIẾT BỊ
═══════════════════════════════════════════════════════════

⚠️  QUAN TRỌNG: Đọc kỹ trước khi thực hiện!

-------------------------------------------------------------------
1. CHUẨN BỊ
-------------------------------------------------------------------

✓ Cáp USB (cùng loại sạc điện thoại Android)
✓ Máy tính Windows/Mac/Linux
✓ Kết nối Internet (để tải driver nếu cần)

-------------------------------------------------------------------
2. CÀI ĐẶT DRIVER (Chỉ lần đầu)
-------------------------------------------------------------------

Windows:
  - Nếu máy KHÔNG NHẬN thiết bị khi cắm USB:
  - Tải driver: https://bit.ly/ch340-driver
  - Cài đặt và khởi động lại máy

Mac/Linux:
  - Thường không cần driver
  - Nếu không nhận: cài python3 và esptool
    → pip3 install esptool

-------------------------------------------------------------------
3. THỰC HIỆN CẬP NHẬT
-------------------------------------------------------------------

WINDOWS:
  1. Kết nối thiết bị qua USB
  2. Double-click file: flash-update.bat
  3. Làm theo hướng dẫn trên màn hình
  4. Đợi 5 phút để hoàn tất

MAC/LINUX:
  1. Kết nối thiết bị qua USB
  2. Mở Terminal
  3. Chạy lệnh: ./flash-update.sh
  4. Làm theo hướng dẫn trên màn hình
  5. Đợi 5 phút để hoàn tất

-------------------------------------------------------------------
4. SAU KHI CẬP NHẬT
-------------------------------------------------------------------

1. Thiết bị tự khởi động lại
2. Thiết bị phát WiFi: ESP32-Config-XXXXXX
3. Kết nối WiFi đó từ điện thoại/máy tính
4. Mở trình duyệt: http://192.168.4.1
5. Nhập thông tin:
   - Tên WiFi (SSID)
   - Mật khẩu WiFi
   - MQTT Server (nếu có)
6. Nhấn "Lưu"
7. Thiết bị tự kết nối và hoạt động bình thường

-------------------------------------------------------------------
5. XỬ LÝ LỖI
-------------------------------------------------------------------

LỖI: "Không tìm thấy thiết bị"
→ Kiểm tra:
  - USB đã cắm chắc chắn?
  - Driver đã cài đặt?
  - Thiết bị đã bật nguồn?
  - Thử cổng USB khác

LỖI: "Xóa/Cài đặt thất bại"
→ Thử lại:
  1. Rút USB, đợi 5 giây, cắm lại
  2. Giữ nút BOOT trên thiết bị
  3. Nhấn nút RESET
  4. Thả RESET (vẫn giữ BOOT)
  5. Chạy lại flash-update

LỖI: "COM port busy"
→ Đóng các chương trình:
  - Arduino IDE
  - Serial Monitor
  - Các tool khác đang dùng COM port

-------------------------------------------------------------------
6. HỖ TRỢ KỸ THUẬT
-------------------------------------------------------------------

Hotline: 0xxx-xxx-xxx (miễn phí)
Email: support@kpltech.vn
Website: https://kpltech.vn/support

═══════════════════════════════════════════════════════════
Version: 1.0 - Nov 2024
═══════════════════════════════════════════════════════════

