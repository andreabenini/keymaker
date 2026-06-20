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
#ifndef DISPLAY_SETUP_H
#define DISPLAY_SETUP_H

#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include <stdbool.h>


/**
 * @brief Initialize and show the setup/configuration screen
 * This screen displays:
 * - Instructions for the user
 * - QR code for WiFi connection
 * - SSID and connection info
 * - Cancel button to return to main screen
 * 
 * @param disp Pointer to the LVGL display object
 */
void display_setup_show(lv_disp_t *disp);

/**
 * @brief Hide the setup screen and return to main screen
 */
void display_setup_hide(void);

/**
 * @brief Update the QR code and WiFi info on the setup screen
 * @param ssid The WiFi AP SSID (e.g., "keymaker-1234")
 * @param url The captive portal URL (e.g., "http://192.168.4.1")
 */
void display_setup_update_qrcode(const char *ssid, const char *url);

/**
 * @brief Check if setup screen is currently visible
 * @return true if setup screen is shown, false otherwise
 */
bool display_setup_is_visible(void);

/**
 * @brief Update status to show portal is ready
 * Changes status label to "Captive portal activated" and shows
 * instruction label with URL to visit
 */
void display_setup_set_status_ready(void);

#endif // DISPLAY_SETUP_H
