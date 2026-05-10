# Keymaker Firmware Installation Guide
This guide will help you install the Keymaker firmware on your ESP32 device
(aka: CYD, Cheap Yellow Display).

## Quick Start (Automatic Setup)
The installer now automatically sets up everything for you
### Current requirements
- **Python 3.6+** or newer (check with `python3 --version`)
- **USB cable** to connect your ESP32
### Installation
1. **Download and extract** the firmware package
2. **Connect** your ESP32 via USB
3. **Run the installer:**
   ```bash
   ./install.sh
   ```
That's it, the installer then will
- Automatically create a Python virtual environment
- Install esptool and dependencies
- Detect your ESP32 device
- Flash the firmware


## License
Keymaker is dual-licensed under GPLv3 for open-source use and commercial licensing
for proprietary products. See the main [README.md](README.md) for details.
