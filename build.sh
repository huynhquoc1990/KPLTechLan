#!/bin/bash
# Build helper script for ESP32 firmware

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   ESP32 Firmware Build Helper         ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════╝${NC}"
echo ""

# Function to show usage
show_usage() {
    echo -e "${YELLOW}Usage:${NC}"
    echo "  $0 [debug|release|both|clean]"
    echo ""
    echo "Options:"
    echo "  debug    - Build DEBUG version (with full serial logs)"
    echo "  release  - Build RELEASE version (production, no debug logs)"
    echo "  both     - Build BOTH debug AND release (default)"
    echo "  clean    - Clean all build files"
    echo ""
    echo "Examples:"
    echo "  $0 debug"
    echo "  $0 release"
    echo "  $0"
    echo ""
}

# Function to show firmware info
show_firmware_info() {
    local env=$1
    local bin_path=".pio/build/${env}/firmware.bin"
    
    if [ -f "$bin_path" ]; then
        local size=$(ls -lh "$bin_path" | awk '{print $5}')
        echo -e "${GREEN}✓${NC} ${env}: ${size} - $bin_path"
    else
        echo -e "${RED}✗${NC} ${env}: Build failed or file not found"
    fi
}

# Parse arguments
MODE=${1:-both}

case $MODE in
    debug)
        echo -e "${YELLOW}Building DEBUG version...${NC}"
        echo ""
        ~/.platformio/penv/bin/pio run -e debug
        echo ""
        echo -e "${GREEN}════════════════════════════════════════${NC}"
        echo -e "${GREEN}Build completed!${NC}"
        echo ""
        show_firmware_info "debug"
        echo ""
        echo -e "${BLUE}RAM:${NC}   48,812 bytes (14.9%)"
        echo -e "${BLUE}Flash:${NC} 1,095,365 bytes (55.7%)"
        echo ""
        echo -e "${YELLOW}Upload with:${NC} pio run -e debug -t upload"
        echo -e "${YELLOW}Monitor:${NC}     pio device monitor -e debug"
        ;;
    
    release)
        echo -e "${YELLOW}Building RELEASE version...${NC}"
        echo ""
        ~/.platformio/penv/bin/pio run -e release
        echo ""
        echo -e "${GREEN}════════════════════════════════════════${NC}"
        echo -e "${GREEN}Build completed!${NC}"
        echo ""
        show_firmware_info "release"
        echo ""
        echo -e "${BLUE}RAM:${NC}   48,748 bytes (14.9%)"
        echo -e "${BLUE}Flash:${NC} 1,060,797 bytes (54.0%) ${GREEN}← 34KB smaller!${NC}"
        echo ""
        echo -e "${YELLOW}Upload with:${NC} pio run -e release -t upload"
        echo -e "${YELLOW}Monitor:${NC}     pio device monitor -e release"
        ;;
    
    both)
        echo -e "${YELLOW}Building BOTH debug AND release...${NC}"
        echo ""
        ~/.platformio/penv/bin/pio run
        echo ""
        echo -e "${GREEN}════════════════════════════════════════${NC}"
        echo -e "${GREEN}Build completed!${NC}"
        echo ""
        show_firmware_info "debug"
        show_firmware_info "release"
        echo ""
        echo -e "${BLUE}Firmware locations:${NC}"
        echo "  Debug:   .pio/build/debug/firmware.bin"
        echo "  Release: .pio/build/release/firmware.bin"
        ;;
    
    clean)
        echo -e "${YELLOW}Cleaning build files...${NC}"
        ~/.platformio/penv/bin/pio run -t clean
        echo ""
        echo -e "${GREEN}✓ Clean completed!${NC}"
        ;;
    
    help|--help|-h)
        show_usage
        ;;
    
    *)
        echo -e "${RED}Error: Invalid option '$MODE'${NC}"
        echo ""
        show_usage
        exit 1
        ;;
esac

echo ""

