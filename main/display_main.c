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

// WiFi connection info
static char g_wifi_ssid[64] = {0};
static char g_wifi_ip[16] = {0};
static int8_t g_wifi_rssi = 0;

// WiFi reconnect request flag
static bool g_wifi_reconnect_requested = false;

// Card state for OTP display
typedef struct {
    int profile_index;              // Index of profile in config
    lv_obj_t *card;                 // Card container
    lv_obj_t *text_container;       // Text container (label/issuer)
    lv_obj_t *otp_label;            // OTP code label (created on tap)
    lv_obj_t *progress_bg;          // Black background that shrinks as progress (created on tap)
    lv_timer_t *timer;              // Timer for progress updates
    uint64_t window_start;          // When current TOTP window started
    uint64_t window_end;            // When current TOTP window ends
    uint32_t period;                // TOTP period for this profile
    bool showing_code;              // True if currently showing OTP code
} card_state_t;

#define MAX_CARD_STATES 20
static card_state_t g_card_states[MAX_CARD_STATES];
static int g_card_state_count = 0;


// Forward declarations
static void hamburger_btn_event_cb(lv_event_t *e);
static void gear_btn_event_cb(lv_event_t *e);
static void wifi_popup_close_cb(lv_event_t *e);
static void wifi_icon_event_cb(lv_event_t *e);
static void rebuild_header_bar(void);
static void rebuild_profile_list(void);
static void card_tap_event_cb(lv_event_t *e);
static void progress_timer_cb(lv_timer_t *timer);


/**
 * 
 */
static void hamburger_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Hamburger button clicked - toggling rotation");
        // Toggle rotation state
        g_is_portrait = !g_is_portrait;
        if (g_is_portrait) {
            // Switch to portrait mode (270 degrees)
            ESP_LOGI(TAG, "Switching to portrait mode");
            g_current_width = LCD_V_RES;
            g_current_height = LCD_H_RES;
            lv_disp_set_rotation(g_disp, LV_DISP_ROT_270);
            esp_lcd_panel_swap_xy(g_panel_handle, true);
            esp_lcd_panel_mirror(g_panel_handle, true, true);
            // Keep touch in landscape calibration mode - LVGL will handle rotation
        } else {
            // Switch to landscape mode (0 degrees)
            ESP_LOGI(TAG, "Switching to landscape mode");
            g_current_width = LCD_H_RES;
            g_current_height = LCD_V_RES;
            lv_disp_set_rotation(g_disp, LV_DISP_ROT_NONE);
            esp_lcd_panel_swap_xy(g_panel_handle, false);
            esp_lcd_panel_mirror(g_panel_handle, true, false);
            // Touch stays in landscape calibration mode (no changes needed)
        }
        rebuild_header_bar();           // Rebuild header bar with new dimensions
        rebuild_profile_list();         // Rebuild profile list with new dimensions
    }
} /**/
