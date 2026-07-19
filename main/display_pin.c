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
#include "display_pin.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display_pin";

// Display resolution (imported from main)
#define LCD_H_RES              320
#define LCD_V_RES              240

// Global UI elements
static lv_obj_t *g_pin_screen = NULL;
static lv_obj_t *g_pin_display_label = NULL;
static lv_obj_t *g_enter_btn = NULL;
static lv_obj_t *g_enter_label = NULL;

// PIN state
static char g_pin_buffer[PIN_MAX_LENGTH + 1] = {0};
static int g_pin_length = 0;
static bool g_pin_complete = false;

// Display and touch handles
static lv_disp_t *g_disp = NULL;
static esp_lcd_panel_handle_t g_panel_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;

// Rotation state
static bool g_is_portrait = false;  // false = landscape, true = portrait
static int g_current_width = LCD_H_RES;
static int g_current_height = LCD_V_RES;

// Forward declarations
static void update_pin_display(void);
static void update_enter_button(void);
static void number_btn_event_cb(lv_event_t *e);
static void backspace_btn_event_cb(lv_event_t *e);
static void enter_btn_event_cb(lv_event_t *e);
static void hamburger_btn_event_cb(lv_event_t *e);
static void rebuild_pin_screen(void);

