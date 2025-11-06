#!/bin/bash

# ============================================================================
# Flash ESP32 with NEW PARTITION TABLE
# This script will ERASE ALL DATA and flash new firmware with corrected partition
# ============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}============================================${NC}"
echo -e "${YELLOW}ESP32 PARTITION TABLE UPDATE${NC}"
echo -e "${YELLOW}============================================${NC}"
echo ""

# Check if port is provided
if [ -z "$1" ]; then
    echo -e "${RED}ERROR: No serial port specified${NC}"
    echo "Usage: $0 <serial_port>"
    echo "Example: $0 /dev/ttyUSB0"
    echo "Example: $0 /dev/cu.usbserial-0001"
    exit 1
fi

PORT=$1

echo -e "${YELLOW}Serial Port: ${PORT}${NC}"
echo ""

# Step 1: Build firmware
echo -e "${GREEN}[1/4] Building firmware with new partition table...${NC}"
~/.platformio/penv/bin/pio run -e release

if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}"
echo ""

# Step 2: Backup old data (optional)
echo -e "${YELLOW}[2/4] Backup old data? (y/n)${NC}"
read -p "Backup LittleFS before erase? (y/n): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${GREEN}Backing up LittleFS...${NC}"
    python3 -m esptool --chip esp32 --port ${PORT} read_flash 0x2B1000 0x200000 littlefs_backup_$(date +%Y%m%d_%H%M%S).bin
    echo -e "${GREEN}✓ Backup saved${NC}"
else
    echo -e "${YELLOW}Skipping backup${NC}"
fi

echo ""

# Step 3: Erase entire Flash
echo -e "${RED}[3/4] ⚠️  ERASING ENTIRE FLASH...${NC}"
echo -e "${RED}This will DELETE ALL DATA (WiFi config, logs, prices)${NC}"
read -p "Continue? (yes/no): " -r
echo ""

if [[ ! $REPLY =~ ^[Yy][Ee][Ss]$ ]]; then
    echo -e "${YELLOW}Aborted by user${NC}"
    exit 0
fi

echo -e "${GREEN}Erasing Flash...${NC}"
python3 -m esptool --chip esp32 --port ${PORT} erase_flash

if [ $? -ne 0 ]; then
    echo -e "${RED}Erase failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Flash erased${NC}"
echo ""

# Step 4: Flash new firmware with new partition table
echo -e "${GREEN}[4/4] Flashing new firmware...${NC}"
~/.platformio/penv/bin/pio run -t upload -e release --upload-port ${PORT}

if [ $? -ne 0 ]; then
    echo -e "${RED}Flash failed!${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Flash successful${NC}"
echo ""

# Summary
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}✓ PARTITION TABLE UPDATE COMPLETE${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "1. Device will reboot with NEW partition table"
echo "2. LittleFS will be EMPTY (no logs, no prices)"
echo "3. WiFi config will be EMPTY - device will enter config portal"
echo "4. Reconnect to WiFi and reconfigure device"
echo "5. Prices and logs will be restored via MQTT"
echo ""
echo -e "${YELLOW}New partition layout:${NC}"
echo "- app0:     1.25 MB (was 1.5 MB)"
echo "- app1:     1.25 MB (was 1.5 MB)"
echo "- LittleFS: 640 KB  (was 2 MB)"
echo "- SPIFFS:   704 KB  (was 2 MB)"
echo "- Total:    4.00 MB ✓"
echo ""
echo -e "${GREEN}Device is ready for operation!${NC}"

