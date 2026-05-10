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

### Installation steps
- **Download Firmware**  
   Download the firmware package which contains:
   - `INSTALL.md`. This detailed guide
   - `QUICK_START.txt`. Quick reference guide
   - `install.sh`. Automatic installation script
   - `requirements.txt`. Python dependencies, held by the installation script
   - `build/` directory with firmware files
   - `AUTHORS.md`. Authors information page
   - `LICENSE`. Project license 
   - `README.md`. Copy of [main README.md](https://github.com/andreabenini/keymaker/blob/main/README.md) file
   - `CHANGELOG.md`. Properly formatted project changelog
   - `VERSION.txt`. Build version and information details

### Connect your ESP32
1. Connect your ESP32 device to your computer using a USB cable
2. The device should appear as a serial port:
   - **Linux**: `/dev/ttyUSB0` or `/dev/ttyACM0`
   - **macOS**: `/dev/cu.usbserial-*` or similar

### Fix permissions (linux only)
If you're on Linux, you may need to add yourself to the `dialout|wheel` or similar
group to access the serial port:
```sh
sudo usermod -a -G dialout $USER
```
Then log out and log back in for the changes to take effect.

### Run the installer
**Automatic port detection:**  
The script will try to automatically detect your ESP32 device.
```bash
./install.sh
```
**Manual port specification:**  
Replace `/dev/ttyUSB0` with your actual port.
```bash
./install.sh /dev/ttyUSB0
```


## License
Keymaker is dual-licensed under GPLv3 for open-source use and commercial licensing
for proprietary products. See the main [README.md](README.md) for details.
