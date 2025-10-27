#!/bin/bash

# Script to build firmware and prepare it for a GitHub Release

# --- Configuration ---
PIO_PATH="$HOME/.platformio/penv/bin/pio"
FIRMWARE_SOURCE=".pio/build/denky32/firmware.bin"
PROJECT_NAME="KPLTechLan"
VERSION_PREFIX="v1.0" # You can change this prefix, e.g., v1.1, v2.0

# --- Script Logic ---
set -e # Exit immediately if a command exits with a non-zero status.

echo "🚀 Starting GitHub Release Preparation..."

# 1. Clean previous builds to ensure a fresh compile
echo "🧹 Cleaning old build files..."
"$PIO_PATH" run -t clean

# 2. Build the firmware
echo "🛠️  Building firmware..."
"$PIO_PATH" run

# 3. Check if build was successful
if [ ! -f "$FIRMWARE_SOURCE" ]; then
    echo "❌ Build failed! Firmware file not found at $FIRMWARE_SOURCE"
    exit 1
fi

echo "✅ Build successful!"

# 4. Create versioned filename
TIMESTAMP=$(date +"%Y%m%d-%H%M")
VERSION_TAG="${VERSION_PREFIX}.${TIMESTAMP}"
DESTINATION_FILE="firmware-${VERSION_TAG}.bin"

# 5. Copy and rename the firmware
echo "📦 Copying firmware to root folder as: $DESTINATION_FILE"
cp "$FIRMWARE_SOURCE" "$DESTINATION_FILE"

# 6. Provide instructions for user
echo ""
echo "🎉 All done! Your firmware is ready for upload."
echo "========================================================================"
echo "Next Steps: Upload to GitHub Releases"
echo ""
echo "1. Go to your GitHub repository releases page:"
echo "   https://github.com/huynhquoc1990/KPLTechLan/releases/new"
echo ""
echo "2. Use the following for the new release:"
echo "   - Tag: ${VERSION_TAG}"
echo "   - Release title: Firmware ${VERSION_TAG}"
echo ""
echo "3. Attach the binary: Drag and drop the file '${DESTINATION_FILE}' from your project folder."
echo ""
echo "4. Click 'Publish release'."
echo ""
echo "5. After publishing, right-click on the '${DESTINATION_FILE}' link and 'Copy Link Address'."
echo "   This is the URL you will use for the OTA update."
echo "========================================================================"
echo ""
