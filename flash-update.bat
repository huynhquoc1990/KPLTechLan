@echo off
chcp 65001 >nul
color 0A

echo.
echo ═══════════════════════════════════════════════════════════
echo   KPL Tech - Công cụ cập nhật thiết bị tự động
echo ═══════════════════════════════════════════════════════════
echo.
echo Thiết bị này cần cập nhật partition table để hoạt động ổn định.
echo Quá trình sẽ mất khoảng 5 phút.
echo.
echo ⚠️  CẢNH BÁO:
echo    - Thiết bị sẽ XÓA HẾT dữ liệu cũ (WiFi, logs)
echo    - Cần cấu hình lại WiFi sau khi hoàn tất
echo    - KHÔNG ĐƯỢC ngắt kết nối USB trong quá trình cập nhật
echo.

pause

REM Detect COM port automatically
echo.
echo [1/4] Đang tìm thiết bị...
set COMPORT=
for /L %%i in (1,1,20) do (
    esptool.exe --port COM%%i flash_id >nul 2>&1
    if not errorlevel 1 (
        set COMPORT=COM%%i
        goto :found
    )
)

echo ❌ KHÔNG TÌM THẤY THIẾT BỊ!
echo.
echo Hãy kiểm tra:
echo - Thiết bị đã kết nối USB?
echo - Driver CH340/CP2102 đã cài đặt?
echo - Thiết bị đang bật nguồn?
echo.
pause
exit /b 1

:found
echo ✓ Tìm thấy thiết bị tại %COMPORT%
echo.

REM Step 2: Erase Flash
echo [2/4] Xóa dữ liệu cũ...
esptool.exe --chip esp32 --port %COMPORT% erase-flash
if errorlevel 1 (
    echo ❌ Xóa thất bại!
    pause
    exit /b 1
)
echo ✓ Xóa thành công
echo.

REM Step 3: Flash new firmware
echo [3/4] Cài đặt firmware mới với partition table mới...
esptool.exe --chip esp32 --port %COMPORT% --baud 460800 ^
  --before default_reset --after hard_reset write_flash -z ^
  --flash_mode dio --flash_freq 80m --flash_size 4MB ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 firmware.bin

if errorlevel 1 (
    echo ❌ Cài đặt thất bại!
    pause
    exit /b 1
)
echo ✓ Cài đặt thành công
echo.

REM Step 4: Complete
echo [4/4] Hoàn tất!
echo.
echo ═══════════════════════════════════════════════════════════
echo   ✓ CẬP NHẬT THÀNH CÔNG!
echo ═══════════════════════════════════════════════════════════
echo.
echo Thiết bị sẽ tự khởi động lại trong 5 giây...
echo.
echo BƯỚC TIẾP THEO:
echo 1. Thiết bị sẽ phát WiFi: ESP32-Config-XXXXXX
echo 2. Kết nối WiFi đó từ điện thoại/máy tính
echo 3. Mở trình duyệt: http://192.168.4.1
echo 4. Nhập lại WiFi và thông tin MQTT
echo 5. Thiết bị sẽ tự kết nối và hoạt động bình thường
echo.
echo Hỗ trợ kỹ thuật: 0xxx-xxx-xxx (miễn phí)
echo.
pause

