# VSCode Setup for Keymaker


## What's already configured
- **ESP-IDF Extension** settings
- **IntelliSense** for code completion and navigation
- **Build tasks** for compilation
- **Flash tasks** for uploading to device
- **Debug configuration** (launch.json)


## Quick Start in VSCode
- ### Open the Project
   ```sh
   code $HOME/Documents/SUSE/projects/keymaker
   ```
- ### Build the Project
   - Press **Ctrl+Shift+B** (default build task)
   - Or: **Ctrl+Shift+P** → "Tasks: Run Task" → "Build - ESP-IDF"
   - Or: Click the ESP-IDF extension icon in the sidebar → Build
- ### Flash to Device
   - **Ctrl+Shift+P** → "Tasks: Run Task" → "Flash - ESP-IDF"
   - Or use the ESP-IDF extension flash button
- ### Monitor Serial Output
   - **Ctrl+Shift+P** → "Tasks: Run Task" → "Monitor - ESP-IDF"
   - Or use the ESP-IDF extension monitor button
   - **To exit**: Press Ctrl+]
- ### All-in-One: Build, Flash, Monitor
   - **Ctrl+Shift+P** → "Tasks: Run Task" → "Build, Flash and Monitor"


## Available Tasks
All tasks are available via **Ctrl+Shift+P**, "Tasks: Run Task":
| Task                     | Description              | Shortcut     |
|--------------------------|--------------------------|--------------|
| Build - ESP-IDF          | Compile the project      | Ctrl+Shift+B |
| Flash - ESP-IDF          | Upload to ESP32          |            - |
| Monitor - ESP-IDF        | View serial output       |            - |
| Build, Flash and Monitor | Do all three in sequence |            - |
| Clean - ESP-IDF          | Full clean rebuild       |            - |


## IntelliSense & Code Navigation
IntelliSense should work automatically. If you see errors:
1. **Rebuild compile_commands.json**:
   ```sh
   . $HOME/.espressif/v5.5.3/esp-idf/export.sh
   idf.py build
   ```
2. **Reload VSCode Window**:
   - **Ctrl+Shift+P**, "Developer: Reload Window"
3. **Select Configuration**:
   - Bottom right: Click on configuration name
   - Select "ESP-IDF"


## ESP-IDF Extension
### Installation (if not installed)
1. Open Extensions: **Ctrl+Shift+X**
2. Search: "ESP-IDF"
3. Install: "Espressif IDF" by Espressif
### Extension Features
- **Build**: Click build icon in status bar
- **Flash**: Click flash icon in status bar
- **Monitor**: Click monitor icon in status bar
- **Device Config**: Click gear icon for menuconfig
- **SDK Config**: Visual menuconfig editor
### Current Configuration
- **IDF Path**: `$HOME/.espressif/v5.5.3/esp-idf`
- **Port**: `/dev/ttyUSB0`
- **Target**: `esp32`
- **Flash Type**: `UART`


## Code Editing Tips
### Keyboard Shortcuts
- **Go to Definition**: F12 or Ctrl+Click
- **Find All References**: Shift+F12
- **Rename Symbol**: F2
- **Format Document**: Shift+Alt+F
- **Show Problems**: Ctrl+Shift+M
- **Toggle Terminal**: Ctrl+\`
### Auto-Complete
- Type and IntelliSense will suggest functions, variables, etc.
- Use arrow keys to navigate suggestions
- Press Tab or Enter to accept
### Snippets
ESP-IDF extension includes snippets for common patterns:
- Type `task` → FreeRTOS task template
- Type `gpio` → GPIO configuration template
- Type `log` → ESP logging macros


## Debugging (Optional Advanced Setup)
Your `launch.json` is configured for OpenOCD debugging.
### Requirements
- ESP32 with JTAG pins connected
- OpenOCD configured for your board
### To Debug
1. Connect JTAG debugger
2. Press **F5** or **Ctrl+Shift+D** → Start Debugging
3. Set breakpoints by clicking left of line numbers


## Troubleshooting
### IntelliSense shows errors but builds fine
- **Solution**: Reload window (Ctrl+Shift+P → "Reload Window")
- Check that `build/compile_commands.json` exists
- Make sure "ESP-IDF" configuration is selected

### Build task fails
- **Check**: Terminal shows "idf.py: command not found"
- **Solution**: Tasks auto-source the IDF environment
- If it still fails, verify `idf.currentSetup` in settings.json

### Port not found
- **Check**: Device is connected (`ls /dev/ttyUSB*`)
- **Update**: Change `idf.port` in settings.json if different

### Extension not working
- **Reinstall**: Remove and reinstall ESP-IDF extension
- **Check**: Extension requires Python 3.x and ESP-IDF installed
