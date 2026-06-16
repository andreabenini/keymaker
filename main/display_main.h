/*
 * The Keymaker - ESP32 CYD OTP Authenticator
 * Copyright (C) 2026 Andrea Benini
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * COMMERCIAL USAGE: If you wish to use this software in a commercial 
 * product or environment where GPLv3 compliance is not possible, 
 * please contact the creator of this repository [andreabenini] @ gmail
 * for a commercial license.
 */
#ifndef DISPLAY_MAIN_H
#define DISPLAY_MAIN_H


#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"


// Color scheme
#define SCREEN_BACKGROUND_COLOR   0x101010  // Dark gray background
#define TITLE_BACKGROUND_COLOR    0x002f9f  // Dark blue header
#define TITLE_FOREGROUND_COLOR    0xFFFFFF  // White text

// WiFi icon colors
#define WIFI_ACTIVE_COLOR         0x00FF00  // Bright green when connected
#define WIFI_INACTIVE_COLOR       0x888888  // Gray when disconnected
#define WIFI_CONNECTING_COLOR     0xFFAA00  // Yellow/orange when connecting

// Profile card colors
#define CARD_BACKGROUND_COLOR     0x2a2a2a  // Slightly lighter gray
#define CARD_TEXT_COLOR           0xFFFFFF  // White for the label field
#define CARD_TEXT_SECONDARY_COLOR 0x00FF00  // Green for the issuer field
#define CARD_ICON_BG_COLOR        0x404040  // Dark gray circle
#define CARD_ICON_TEXT_COLOR      0xFF0000  // Red for icon text

// OTP display colors
#define OTP_CODE_COLOR            0xFFFFFF  // White for OTP code
#define OTP_PROGRESS_BG_COLOR     0x000000  // Black background for OTP progress

#endif // DISPLAY_MAIN_H
