# Keymaker - Setup Guide
## First boot and calibration
- **Touch Screen Calibration (REQUIRED)**  
    The Keymaker uses a resistive touchscreen that **requires calibration** for accurate touch detection.
- **First Boot Calibration**  
    On first boot (or after factory reset), the device will **automatically enter calibration mode**:
    1. **Four targets will appear** - one in each corner of the screen
    2. **Touch each target** carefully with the stylus, aiming for the center dot
    3. The target will flash **green** when touch is detected
    4. **Wait for all 4 targets** to complete
    5. Calibration data is **automatically saved** to NVS (non-volatile device storage)
- **Calibration Persistence**
    - Calibration **survives reflashing** (`idf.py flash`) - you only need to calibrate once!
    - Calibration persists across power cycles
    - Calibration is **LOST** on `idf.py erase-flash` or factory reset only
- **Manual Recalibration**, to recalibrate the touch screen later:
    1. Open the **Settings** menu (gear icon)
    2. Select **"Touch Calibration"**
    3. Follow the on-screen instructions


## Hardware Configuration
These settings have been detected during the project creation
- **Display (ILI9341) - SPI2**
    - **SCLK**: GPIO 14
    - **MOSI**: GPIO 13
    - **MISO**: GPIO 12
    - **CS**: GPIO 15
    - **DC**: GPIO 2
    - **Backlight**: GPIO 21
    - **Resolution**: 320x240
- **Touch (XPT2046) - SPI3**
    - **SCLK**: GPIO 25
    - **MOSI**: GPIO 32
    - **MISO**: GPIO 39
    - **CS**: GPIO 33
    - **IRQ**: GPIO 36


