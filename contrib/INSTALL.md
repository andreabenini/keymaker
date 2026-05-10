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

## Detailed guide
### Prerequisites
- Python 3  
  You need Python 3.6 or later installed on your system.  
  **Check if Python is installed:**
  ```sh
   python3 --version
  ```
   - **If not installed:**
      - **Linux (Arch)**: `sudo pacman -Sy python3`
      - **Linux (SUSE\*)**: `sudo zypper install python3`
      - **Linux (Debian/Ubuntu)**: `sudo apt install python3 python3-venv`
      - **Linux (Fedora)**: `sudo dnf install python3`
      - **macOS**: `brew install python3` (requires [Homebrew](https://brew.sh))


## License
Keymaker is dual-licensed under GPLv3 for open-source use and commercial licensing
for proprietary products. See the main [README.md](README.md) for details.
