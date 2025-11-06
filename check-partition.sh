#!/bin/bash

# ============================================================================
# Check ESP32 Partition Table
# Determine if device has OLD (2MB) or NEW (640KB) partition
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ -z "$1" ]; then
    echo -e "${RED}ERROR: No serial port specified${NC}"
    echo "Usage: $0 <serial_port>"
    echo "Example: $0 /dev/ttyUSB0"
    exit 1
fi

PORT=$1

echo -e "${YELLOW}============================================${NC}"
echo -e "${YELLOW}ESP32 PARTITION TABLE CHECK${NC}"
echo -e "${YELLOW}============================================${NC}"
echo ""

# Read partition table from device
echo -e "${GREEN}Reading partition table from ${PORT}...${NC}"
python3 -m esptool --chip esp32 --port ${PORT} read_flash 0x8000 0xC00 /tmp/partition_check.bin

if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to read partition table!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}Parsing partition table...${NC}"
echo ""

# Parse and display
python3 << 'EOF'
import struct
import sys

def parse_partition(data):
    """Parse ESP32 partition table entry"""
    magic = struct.unpack('<H', data[0:2])[0]
    if magic != 0x50AA:
        return None
    
    ptype = data[2]
    subtype = data[3]
    offset = struct.unpack('<I', data[4:8])[0]
    size = struct.unpack('<I', data[8:12])[0]
    name = data[12:28].decode('ascii', errors='ignore').strip('\x00')
    
    return {
        'type': ptype,
        'subtype': subtype,
        'offset': offset,
        'size': size,
        'name': name
    }

# Read partition table
with open('/tmp/partition_check.bin', 'rb') as f:
    data = f.read()

# Parse entries
partitions = []
for i in range(0, len(data), 32):
    entry = parse_partition(data[i:i+32])
    if entry and entry['name']:
        partitions.append(entry)

# Display
print("Partition Table:")
print("-" * 80)
for p in partitions:
    size_kb = p['size'] / 1024
    size_mb = p['size'] / 1024 / 1024
    if size_mb >= 1:
        size_str = f"{size_mb:.2f} MB"
    else:
        size_str = f"{size_kb:.1f} KB"
    
    print(f"{p['name']:12s}  Offset: 0x{p['offset']:06X}  Size: 0x{p['size']:06X} ({size_str:>10s})")
    
    # Check LittleFS
    if p['name'] == 'littlefs':
        print("")
        if p['size'] == 0x200000:  # 2MB
            print("❌ OLD PARTITION: LittleFS = 2 MB")
            print("   This is the PROBLEMATIC partition causing divide-by-zero!")
            print("   End offset: 0x{:06X} (likely > 0x400000 for 4MB Flash)".format(p['offset'] + p['size']))
            print("")
            print("   ACTION REQUIRED: Flash new partition table via USB")
            print("   Run: ./flash-new-partition.sh {}".format(sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'))
            sys.exit(1)
        elif p['size'] == 0xA0000:  # 640KB
            print("✅ NEW PARTITION: LittleFS = 640 KB")
            print("   Partition is correct and will not cause divide-by-zero")
            print("   End offset: 0x{:06X} (within 4MB Flash ✓)".format(p['offset'] + p['size']))
            sys.exit(0)
        else:
            print(f"⚠️  UNKNOWN PARTITION SIZE: {p['size']} bytes")
            sys.exit(2)

print("")
print("⚠️  LittleFS partition not found in table!")
sys.exit(2)
EOF

if [ $? -eq 1 ]; then
    echo ""
    echo -e "${RED}============================================${NC}"
    echo -e "${RED}⚠️  OLD PARTITION DETECTED - NEEDS UPDATE${NC}"
    echo -e "${RED}============================================${NC}"
    echo ""
    echo -e "${YELLOW}To fix, run:${NC}"
    echo -e "  ${GREEN}./flash-new-partition.sh ${PORT}${NC}"
    exit 1
elif [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}============================================${NC}"
    echo -e "${GREEN}✅ NEW PARTITION - OK${NC}"
    echo -e "${GREEN}============================================${NC}"
    exit 0
else
    echo ""
    echo -e "${YELLOW}Unable to determine partition status${NC}"
    exit 2
fi

