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
#ifndef WIFI_ERROR_H
#define WIFI_ERROR_H

#include "lvgl.h"

// WiFi error screen colors
#define WIFI_ERROR_BG_COLOR   0xFFFFFF  // White background
#define WIFI_ERROR_TEXT_COLOR 0x000000  // Black text


/**
 * @brief Show WiFi error screen
 * Displays a white screen with black text saying "Not connected to wifi"
 *
 * @param disp Pointer to the LVGL display object
 */
void wifi_error_show(lv_disp_t *disp);


/**
 * @brief Hide WiFi error screen
 * Removes the error screen and returns to previous screen
 */
void wifi_error_hide(void);


#endif // WIFI_ERROR_H
