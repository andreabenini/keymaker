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
#include "display_main.h"
#include "display_setup.h"
#include "portal.h"
#include "config.h"
#include "totp.h"
#include "time_sync.h"
#include "wifi_error.h"
#include "esp_log.h"
#include <stdlib.h>

// Display resolution (imported from main)
#define LCD_H_RES              320
#define LCD_V_RES              240


static const char *TAG = "display_main";

// Current display resolution (updated on rotation)
static int g_current_width = LCD_H_RES;
static int g_current_height = LCD_V_RES;

// Global UI elements
static lv_obj_t *g_header_bar = NULL;
static lv_obj_t *g_hamburger_btn = NULL;
static lv_obj_t *g_gear_btn = NULL;
static lv_obj_t *g_wifi_icon = NULL;
static lv_obj_t *g_profile_list = NULL;

// Rotation state
static lv_disp_t *g_disp = NULL;
static esp_lcd_panel_handle_t g_panel_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;
static bool g_is_portrait = false;  // false = landscape, true = portrait

// WiFi state tracking
typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

static wifi_state_t g_wifi_state = WIFI_STATE_DISCONNECTED;
static bool g_wifi_connected = false;  // Keep for backwards compatibility

