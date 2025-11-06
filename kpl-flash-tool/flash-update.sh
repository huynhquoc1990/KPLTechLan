#!/bin/bash

# ============================================================================
# KPL Tech - Flash Update Tool
# Tự động cập nhật partition table cho thiết bị
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

clear
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  KPL Tech - Công cụ cập nhật thiết bị tự động${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo ""
echo "Thiết bị này cần cập nhật partition table để hoạt động ổn định."
echo "Quá trình sẽ mất khoảng 5 phút."
echo ""
echo -e "${YELLOW}⚠️  CẢNH BÁO:${NC}"
echo "   - Thiết bị sẽ XÓA HẾT dữ liệu cũ (WiFi, logs)"
echo "   - Cần cấu hình lại WiFi sau khi hoàn tất"
echo "   - KHÔNG ĐƯỢC ngắt kết nối USB trong quá trình cập nhật"
echo ""
read -p "Nhấn Enter để tiếp tục hoặc Ctrl+C để hủy..."
echo ""

# Step 1: Detect device
echo -e "${BLUE}[1/4] Đang tìm thiết bị...${NC}"

PORTS=("/dev/ttyUSB0" "/dev/ttyUSB1" "/dev/cu.usbserial-0001" "/dev/cu.usbserial-21120" "/dev/cu.wchusbserial*")
FOUND_PORT=""

for PORT_PATTERN in "${PORTS[@]}"; do
    for PORT in $PORT_PATTERN; do
        if [ -e "$PORT" ]; then
            python3 -m esptool --chip esp32 --port $PORT flash_id &>/dev/null
            if [ $? -eq 0 ]; then
                FOUND_PORT=$PORT
                break 2
            fi
        fi
    done
done

if [ -z "$FOUND_PORT" ]; then
    echo -e "${RED}❌ KHÔNG TÌM THẤY THIẾT BỊ!${NC}"
    echo ""
    echo "Hãy kiểm tra:"
    echo "- Thiết bị đã kết nối USB?"
    echo "- Driver CH340/CP2102 đã cài đặt?"
    echo "- Thiết bị đang bật nguồn?"
    echo ""
    exit 1
fi

echo -e "${GREEN}✓ Tìm thấy thiết bị tại: $FOUND_PORT${NC}"
echo ""

# Step 2: Erase Flash
echo -e "${BLUE}[2/4] Xóa dữ liệu cũ...${NC}"
python3 -m esptool --chip esp32 --port $FOUND_PORT erase-flash

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Xóa thất bại!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Xóa thành công${NC}"
echo ""

# Step 3: Flash firmware
echo -e "${BLUE}[3/4] Cài đặt firmware mới với partition table mới...${NC}"
python3 -m esptool --chip esp32 --port $FOUND_PORT --baud 460800 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x1000 .pio/build/release/bootloader.bin \
  0x8000 .pio/build/release/partitions.bin \
  0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/release/firmware.bin

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Cài đặt thất bại!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Cài đặt thành công${NC}"
echo ""

# Step 4: Complete
echo -e "${BLUE}[4/4] Hoàn tất!${NC}"
echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  ✓ CẬP NHẬT THÀNH CÔNG!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
echo ""
echo "Thiết bị sẽ tự khởi động lại trong 5 giây..."
echo ""
echo -e "${YELLOW}BƯỚC TIẾP THEO:${NC}"
echo "1. Thiết bị sẽ phát WiFi: ESP32-Config-XXXXXX"
echo "2. Kết nối WiFi đó từ điện thoại/máy tính"
echo "3. Mở trình duyệt: http://192.168.4.1"
echo "4. Nhập lại WiFi và thông tin MQTT"
echo "5. Thiết bị sẽ tự kết nối và hoạt động bình thường"
echo ""
echo "Hỗ trợ kỹ thuật: 0xxx-xxx-xxx (miễn phí)"
echo ""

