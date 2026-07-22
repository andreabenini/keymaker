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

/**
 * @brief Update the PIN display with asterisks
 */
static void update_pin_display(void) {
    char display_text[PIN_MAX_LENGTH + 1];
    memset(display_text, 0, sizeof(display_text));
    for (int i = 0; i < g_pin_length; i++) {
        display_text[i] = '*';
    }
    lv_label_set_text(g_pin_display_label, display_text);
} /**/


/**
 * Update ENTER button state based on PIN length
 */
static void update_enter_button(void) {
    if (g_pin_length >= PIN_MIN_LENGTH) {
        // Enable ENTER button - green with white symbol
        lv_obj_set_style_bg_color(g_enter_btn, lv_color_hex(PIN_BUTTON_ENTER_ENABLED_COLOR), 0);
        lv_obj_set_style_text_color(g_enter_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0); // White symbol (visible on green)
        lv_obj_clear_state(g_enter_btn, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Enter button ENABLED - disabled state cleared");
    } else {
        // Disable ENTER button - gray with white symbol
        lv_obj_set_style_bg_color(g_enter_btn, lv_color_hex(PIN_BUTTON_ENTER_DISABLED_COLOR), 0);
        lv_obj_set_style_text_color(g_enter_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0); // White symbol (visible on gray)
        lv_obj_add_state(g_enter_btn, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Enter button DISABLED - disabled state set");
    }
} /**/


/**
 * Number button click handler
 */
static void number_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    // Provide visual feedback on press/release
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);  // Lighter on press
        return;
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_BUTTON_BG_COLOR), 0);  // Back to normal
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Number button %d clicked (portrait=%d, width=%d, height=%d)", digit, g_is_portrait, g_current_width, g_current_height);

    // Check if we can add more digits
    if (g_pin_length >= PIN_MAX_LENGTH) {
        ESP_LOGW(TAG, "PIN already at max length (%d)", PIN_MAX_LENGTH);
        return;
    }

    // Add digit to PIN
    g_pin_buffer[g_pin_length] = '0' + digit;
    g_pin_length++;
    g_pin_buffer[g_pin_length] = '\0';
    ESP_LOGI(TAG, "PIN length: %d", g_pin_length);
    update_pin_display();
    update_enter_button();
} /**/
