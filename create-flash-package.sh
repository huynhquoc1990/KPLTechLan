#!/bin/bash

# ============================================================================
# Create Flash Tool Package for Distribution
# Tạo package tool flash cho user
# ============================================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}Creating flash tool package...${NC}"

# Create package directory
PACKAGE_DIR="kpl-flash-tool-$(date +%Y%m%d)"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

# Build firmware first
echo -e "${YELLOW}Building firmware...${NC}"
~/.platformio/penv/bin/pio run -e release

# Copy binary files
echo -e "${YELLOW}Copying binary files...${NC}"
cp .pio/build/release/bootloader.bin "$PACKAGE_DIR/"
cp .pio/build/release/partitions.bin "$PACKAGE_DIR/"
cp .pio/build/release/firmware.bin "$PACKAGE_DIR/"
cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin "$PACKAGE_DIR/"

# Copy scripts
echo -e "${YELLOW}Copying scripts...${NC}"
cp flash-update.sh "$PACKAGE_DIR/"
cp flash-update.bat "$PACKAGE_DIR/"
cp kpl-flash-tool/README.txt "$PACKAGE_DIR/"
chmod +x "$PACKAGE_DIR/flash-update.sh"

# Download esptool for Windows (optional)
echo -e "${YELLOW}Adding Windows esptool...${NC}"
if [ ! -f "esptool.exe" ]; then
    echo "Note: Download esptool.exe manually from:"
    echo "https://github.com/espressif/esptool/releases"
    echo "and place in package directory"
fi

# Create version info
echo -e "${YELLOW}Creating version info...${NC}"
cat > "$PACKAGE_DIR/VERSION.txt" << EOF
KPL Tech Flash Tool
Version: $(date +%Y.%m.%d)
Firmware: $(git rev-parse --short HEAD)
Built: $(date)

Files included:
- bootloader.bin    (ESP32 bootloader)
- partitions.bin    (Partition table with LittleFS 640KB)
- firmware.bin      (Main application)
- boot_app0.bin     (OTA boot selector)
- flash-update.sh   (Mac/Linux flash script)
- flash-update.bat  (Windows flash script)
- README.txt        (User instructions in Vietnamese)

Requirements:
- Python 3 + esptool
- USB cable
- ESP32 device with 4MB Flash
EOF

# Create zip package
echo -e "${YELLOW}Creating zip package...${NC}"
zip -r "${PACKAGE_DIR}.zip" "$PACKAGE_DIR"

# Show results
echo -e "${GREEN}Package created successfully!${NC}"
echo ""
echo "Package: ${PACKAGE_DIR}.zip"
echo "Size: $(du -h "${PACKAGE_DIR}.zip" | cut -f1)"
echo ""
echo "Contents:"
ls -lh "$PACKAGE_DIR"
echo ""
echo -e "${GREEN}Ready for distribution!${NC}"
echo ""
echo "Next steps:"
echo "1. Upload ${PACKAGE_DIR}.zip to server"
echo "2. Create landing page: https://kpltech.vn/flash-tool"
echo "3. Test on devices with old partition"
echo "4. OTA new firmware to all devices"
echo "5. Send notifications to customers"




