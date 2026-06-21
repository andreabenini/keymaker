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
#ifndef DISPLAY_PIN_H
#define DISPLAY_PIN_H


// Color scheme matching main menu
#define PIN_SCREEN_BACKGROUND_COLOR   0x101010      // Dark gray background
#define PIN_TITLE_BACKGROUND_COLOR    0x002f9f      // Dark blue header
#define PIN_TITLE_FOREGROUND_COLOR    0xFFFFFF      // White text

// PIN display colors
#define PIN_DISPLAY_COLOR             0xFFFFFF      // White asterisks
#define PIN_DISPLAY_BG_COLOR          0x202020      // Slightly lighter than screen background

// Keypad button colors
#define PIN_BUTTON_BG_COLOR           0x2a2a2a      // Same as profile cards
#define PIN_BUTTON_TEXT_COLOR         0xFFFFFF      // White text
#define PIN_BUTTON_BACKSPACE_COLOR    0xFF0000      // Red for backspace
#define PIN_BUTTON_ENTER_DISABLED_COLOR 0x888888    // Gray when disabled
#define PIN_BUTTON_ENTER_ENABLED_COLOR  0x00FF00    // Green when enabled

// Sizing
#define PIN_HEADER_HEIGHT             35            // Height of blue title bar
#define PIN_BUTTON_WIDTH              96            // Button width (rectangular)
#define PIN_BUTTON_HEIGHT             45            // Button height (rectangular)
#define PIN_BUTTON_GAP                5             // Gap between buttons
#define PIN_FONT_SIZE_HEADER          16            // Font size for "PIN:" label
#define PIN_FONT_SIZE_DISPLAY         20            // Font size for asterisks
#define PIN_FONT_SIZE_BUTTON          32            // Font size for button numbers

// PIN constraints
#define PIN_MIN_LENGTH                1             // Minimum 1 character
#define PIN_MAX_LENGTH                8             // Maximum 8 characters

#endif // DISPLAY_PIN_H
