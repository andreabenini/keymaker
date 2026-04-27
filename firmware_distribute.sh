#!/bin/bash
#
# Populate distribution directory 'dist' and create firmware package for CYD
# Prepares the dist/ directory with firmware and installer for easy distribution
#
set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'
print_info()    { echo -e "${BLUE}->${NC} $1"; }
print_success() { echo -e "${GREEN}   ✓${NC} $1"; }
print_warning() { echo -e "${YELLOW}⚠${NC} $1"; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${SCRIPT_DIR}/dist"
print_info "Populating distribution directory..."

# Clean and recreate dist directory
if [ -d "$DIST_DIR" ]; then
    print_info "Cleaning existing dist directory..."
    rm -rf "${DIST_DIR:?}"/*
fi
mkdir -p "$DIST_DIR"
# Check if firmware has been built
if [ ! -f "${SCRIPT_DIR}/build/keymaker.bin" ]; then
    print_warning "Firmware not found! Please build the project first:"
    echo "  idf.py build"
    exit 1
fi

# Copy firmware files
print_info "Copying firmware binaries..."
mkdir -p "${DIST_DIR}/build/bootloader"
mkdir -p "${DIST_DIR}/build/partition_table"
cp "${SCRIPT_DIR}/build/bootloader/bootloader.bin" "${DIST_DIR}/build/bootloader/"
cp "${SCRIPT_DIR}/build/partition_table/partition-table.bin" "${DIST_DIR}/build/partition_table/"
cp "${SCRIPT_DIR}/build/keymaker.bin" "${DIST_DIR}/build/"

# Copy installer and dependencies
print_info "Copying installation files..."
cp "${SCRIPT_DIR}/contrib/install.sh" "${DIST_DIR}/"
cp "${SCRIPT_DIR}/contrib/requirements.txt" "${DIST_DIR}/"
chmod +x "${DIST_DIR}/install.sh"

# Copy documentation
print_info "Copying documentation..."
cp "${SCRIPT_DIR}/contrib/INSTALL.md" "${DIST_DIR}/"
cp "${SCRIPT_DIR}/README.md" "${DIST_DIR}/"
[ -f "${SCRIPT_DIR}/LICENSE" ] && cp "${SCRIPT_DIR}/LICENSE" "${DIST_DIR}/"

# Create a quick start guide
cat > "${DIST_DIR}/QUICK_START.txt" << 'EOF'

TheKeymaker Firmware  -  Quick Start Guide
═══════════════════════════════════════════════════════════════════════════

- Connect your ESP32 device via USB
- Run the installer
        ./install.sh
  The installer will:
        ✓ Automatically set up Python virtualenv (if needed)
        ✓ Install esptool (if needed)
        ✓ Detect your ESP32 device
        ✓ Flash the firmware
- For detailed instructions, see INSTALL.md
- Need help? Check README.md or visit https://github.com/andreabenini/keymaker

EOF
cp "${SCRIPT_DIR}/AUTHORS.md" "${DIST_DIR}/AUTHORS.md"

# Create version info
VERSION="unknown"
if git describe --tags --always &> /dev/null 2>&1; then
    VERSION=$(git describe --tags --always --dirty)
elif [ -f "${SCRIPT_DIR}/build/keymaker.bin" ]; then
    VERSION=$(date -r "${SCRIPT_DIR}/build/keymaker.bin" +%Y%m%d-%H%M%S)
fi
IDF_VERSION=$(cat ${SCRIPT_DIR}/flash.sh | sed -n 's|.*/v\([0-9.]*\)/.*|\1|p')
cat > "${DIST_DIR}/VERSION.txt" << EOF
Keymaker Firmware

Version: ${VERSION}
Build Date: $(date +"%Y-%m-%d %H:%M:%S")
ESP-IDF: ${IDF_VERSION}
Target: ESP32 (CYD)
EOF

# Summary
ITEM_LIST='       - %-50s%10s\n'
print_success "Distribution directory populated: ${DIST_DIR}/"
print_success "Contents"
ls -lh "$DIST_DIR" | tail -n +2 | awk -v fmt="$ITEM_LIST" '{printf fmt, $9, $5}'
print_success "Firmware files"
ls -lh "${DIST_DIR}/build/"*.bin "${DIST_DIR}/build/"*/*.bin 2>/dev/null | sed "s#${SCRIPT_DIR}/dist#.#" | awk -v fmt="$ITEM_LIST" '{printf fmt, $9, $5}'
echo ""
print_info "Distribution created, to install:"
echo "       - Share '${DIST_DIR}/' folder"
echo "       - Create an archive: tar czf keymaker-${VERSION}.tar.gz -C dist ."
print_success "Simply run: ./install.sh within ./dist directory\n"
