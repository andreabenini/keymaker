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
   code /home/ben/Documents/SUSE/projects/keymaker
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

