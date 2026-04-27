#!/bin/bash

#
# Keymaker Firmware Installer
# Simple script to flash pre-built firmware to ESP32 devices
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# dummy functions here used for producing neat display information
print_info()    { echo -e "${BLUE}🛈${NC} $1";   }
print_success() { echo -e "${GREEN}✓${NC} $1";   }
print_warning() { echo -e "${YELLOW}⚠${NC} $1"; }
print_error()   { echo -e "${RED}✗${NC} $1";     }
line() {
    printf '%*s\r' "${COLUMNS:-$(tput cols)}" '' | tr ' ' -
    echo -e "${GREEN}[$1] ${NC}"
}

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Banner
echo -e "${GREEN}[ The KeyMaker firmware installer ]"
line " ESP32 OTP Authenticator         "
echo -e "\nThis script will:"
print_success "Create or activate a python virtualenv within this dir ($(pwd))"
print_success "Install required python pip packages (see requirements.txt)"
print_success "Flash your CYD display with newly installed esptool.py"
echo -en "\npress any key to continue, Ctrl+C to exit..."; read -n1 junk
exit 0

# Setup Python environment
print_info "Setting up Python environment..."

# Check if virtualenv exists in the distribution
# Always use a virtualenv for the installation
VENV_DIR="${SCRIPT_DIR}/venv"
if [ -d "$VENV_DIR" ]; then
    print_info "Found existing virtual environment"
    source "${VENV_DIR}/bin/activate"
    print_success "Virtual environment activated"
elif [ -f "${SCRIPT_DIR}/requirements.txt" ]; then
    # Check if Python is available
    if ! command -v python3 &> /dev/null; then
        print_error "Python 3 is not installed!"
        echo ""
        echo "Please install Python 3 and try again."
        echo "Visit: https://www.python.org/downloads/"
        exit 1
    fi
    print_info "Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
    source "${VENV_DIR}/bin/activate"

    print_info "Installing dependencies from requirements.txt..."
    pip install --quiet --upgrade pip
    pip install --quiet -r "${SCRIPT_DIR}/requirements.txt"
    print_success "Virtual environment created and configured"
fi

# Check if esptool is installed
print_info "Checking for esptool.py..."
if ! command -v esptool.py &> /dev/null; then
    print_error "esptool.py not found!"
    echo ""
    if [ -f "${SCRIPT_DIR}/requirements.txt" ]; then
        echo "Something went wrong with the automatic setup."
        echo "Please try manually:"
        echo ""
        echo "  ${GREEN}python3 -m venv venv${NC}"
        echo "  ${GREEN}source venv/bin/activate${NC}"
        echo "  ${GREEN}pip install -r requirements.txt${NC}"
        echo ""
        echo "Then run this script again."
    else
        echo "Please install esptool using one of these methods:"
        echo ""
        echo "  Python pip (recommended):"
        echo "    ${GREEN}pip install esptool${NC}"
        echo ""
        echo "  Or via pip3:"
        echo "    ${GREEN}pip3 install esptool${NC}"
        echo ""
        echo "After installation, run this script again."
    fi
    exit 1
fi
print_success "esptool.py found: $(which esptool.py)"

# Check firmware files
print_info "Checking firmware files..."
BUILD_DIR="${SCRIPT_DIR}/build"

BOOTLOADER="${BUILD_DIR}/bootloader/bootloader.bin"
PARTITION="${BUILD_DIR}/partition_table/partition-table.bin"
FIRMWARE="${BUILD_DIR}/keymaker.bin"

MISSING_FILES=0
if [ ! -f "$BOOTLOADER" ]; then
    print_error "Bootloader not found: $BOOTLOADER"
    MISSING_FILES=1
fi
if [ ! -f "$PARTITION" ]; then
    print_error "Partition table not found: $PARTITION"
    MISSING_FILES=1
fi
if [ ! -f "$FIRMWARE" ]; then
    print_error "Firmware not found: $FIRMWARE"
    MISSING_FILES=1
fi

if [ $MISSING_FILES -eq 1 ]; then
    echo ""
    print_error "Missing firmware files! Please ensure all .bin files are in the build directory."
    exit 1
fi
print_success "All firmware files found"

# Detect or ask for USB port
print_info "Detecting ESP32 device..."

PORT=""
if [ -n "$1" ]; then
    # Port provided as argument
    PORT="$1"
    print_info "Using specified port: $PORT"
else
    # Auto-detect common USB serial ports
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux - check common USB serial devices
        POSSIBLE_PORTS=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS - check USB serial devices
        POSSIBLE_PORTS=$(ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.wchusbserial* 2>/dev/null || true)
    else
        # Windoze ? none of my business
        print_warning "Unsupported OS: $OSTYPE"
        POSSIBLE_PORTS=""
    fi

    # Count available ports
    PORT_COUNT=$(echo "$POSSIBLE_PORTS" | grep -c "/dev/" || echo "0")

    if [ "$PORT_COUNT" -eq 0 ]; then
        print_warning "No USB serial device detected automatically."
        echo ""
        echo "Please specify the port manually:"
        echo "  Usage: $0 /dev/ttyUSB0"
        echo ""
        echo "Common ports:"
        echo "  Linux:  /dev/ttyUSB0, /dev/ttyACM0"
        echo "  macOS:  /dev/cu.usbserial-*"
        exit 1
    elif [ "$PORT_COUNT" -eq 1 ]; then
        PORT=$(echo "$POSSIBLE_PORTS" | head -n 1)
        print_success "Auto-detected port: $PORT"
    else
        print_warning "Multiple USB serial devices found:"
        echo "$POSSIBLE_PORTS" | nl
        echo ""
        echo "Please specify which port to use:"
        echo "  Usage: $0 <port>"
        exit 1
    fi
fi

# Verify port exists
if [ ! -e "$PORT" ]; then
    print_error "Port $PORT does not exist!"
    exit 1
fi

# Check chip info
print_info "Reading chip information..."
if ! esptool.py --port "$PORT" chip_id 2>&1 | head -n 10; then
    print_error "Failed to communicate with ESP32!"
    echo ""
    echo "Troubleshooting:"
    echo "  1. Ensure the ESP32 is connected via USB"
    echo "  2. Check that you have permission to access $PORT"
    echo "     (you may need to add yourself to the 'dialout' group on Linux)"
    echo "  3. Try pressing the BOOT button while connecting"
    exit 1
fi

# Confirmation
echo ""
print_warning "Ready to flash firmware to $PORT"
echo ""
echo "This will:"
echo "  • Erase the current firmware"
echo "  • Flash bootloader at 0x1000"
echo "  • Flash partition table at 0x8000"
echo "  • Flash Keymaker firmware at 0x10000"
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    print_info "Aborted by user"
    exit 0
fi

# Flash firmware
print_info "Flashing firmware..."
echo ""

esptool.py \
    --chip esp32 \
    --port "$PORT" \
    --baud 460800 \
    --before default_reset \
    --after hard_reset \
    write_flash \
    --flash_mode dio \
    --flash_freq 40m \
    --flash_size 4MB \
    0x1000 "$BOOTLOADER" \
    0x8000 "$PARTITION" \
    0x10000 "$FIRMWARE"

# Success
echo ""
print_success "Firmware flashed successfully!"
echo ""
print_info "Your Keymaker device should now be rebooting..."
print_info "You can monitor the serial output with:"
echo "  esptool.py --port $PORT monitor"
echo ""
echo -e "${BLUE}Thank you for using Keymaker!${NC}"
