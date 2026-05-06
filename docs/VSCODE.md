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


