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
static void card_hide_otp_code(card_state_t *state);
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


/**
 * 
 */
static void gear_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    ESP_LOGI(TAG, "Gear button event: %d", code);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Gear button clicked - entering setup mode");
        // Check if portal is still stopping from previous session
        if (portal_is_stopping()) {
            ESP_LOGW(TAG, "Portal is still stopping, please wait a moment");
            // TODO: Could show a toast message here
            return;
        }
        // Start the captive portal
        esp_err_t ret = portal_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start portal: %s", esp_err_to_name(ret));
            return;
        }
        // Show setup screen
        ESP_LOGI(TAG, "Showing setup screen...");
        display_setup_show(g_disp);
        // Update QR code with portal info
        ESP_LOGI(TAG, "Updating QR code with SSID=%s, URL=%s", portal_get_ssid(), portal_get_url());
        display_setup_update_qrcode(portal_get_ssid(), portal_get_url());
        // Update status to show portal is ready
        display_setup_set_status_ready();
        ESP_LOGI(TAG, "Setup mode activated");
    }
} /**/


/**
 * Closing wifi popup window
 */
static void wifi_popup_close_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t *popup = (lv_obj_t *)lv_event_get_user_data(e);
        if (popup) {
            lv_obj_del(popup);
        }
    }
} /**/


/**
 * Wifi icon even handler
 */
static void wifi_icon_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "WiFi icon clicked - state=%d", g_wifi_state);
        if (g_wifi_state == WIFI_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Showing WiFi connection info");                      // Show connection info popup
            lv_obj_t *scr = lv_disp_get_scr_act(g_disp);                        // Create message box on the current screen
            // Create a simple popup instead of msgbox for better control
            lv_obj_t *popup = lv_obj_create(scr);
            lv_obj_set_size(popup, g_current_width - 40, 180);
            lv_obj_center(popup);
            lv_obj_set_style_bg_color(popup, lv_color_hex(0x2a2a2a), 0);
            lv_obj_set_style_border_color(popup, lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_border_width(popup, 2, 0);
            lv_obj_set_style_radius(popup, 10, 0);
            lv_obj_set_style_pad_all(popup, 15, 0);
            lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

            // Title
            lv_obj_t *title = lv_label_create(popup);
            lv_label_set_text(title, "WiFi Connection");
            lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(title, 0, 0);

            // Build info text
            char info_text[256];
            int rssi_bars = 0;
            if (g_wifi_rssi >= -50) rssi_bars = 4;
            else if (g_wifi_rssi >= -60) rssi_bars = 3;
            else if (g_wifi_rssi >= -70) rssi_bars = 2;
            else rssi_bars = 1;
            snprintf(info_text, sizeof(info_text), "SSID: %s\n" "IP: %s\n" "Signal: %d dBm (%d/4 bars)", g_wifi_ssid, g_wifi_ip, g_wifi_rssi, rssi_bars);

            // Info text
            lv_obj_t *text = lv_label_create(popup);
            lv_label_set_text(text, info_text);
            lv_obj_set_style_text_color(text, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_pos(text, 0, 30);

            // Close button (centered at bottom)
            lv_obj_t *close_btn = lv_btn_create(popup);
            lv_obj_set_size(close_btn, 100, 40);
            lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -10);                           // Centered horizontally, 10px from bottom

            lv_obj_t *close_label = lv_label_create(close_btn);
            lv_label_set_text(close_label, "Close");
            lv_obj_center(close_label);
            lv_obj_clear_flag(close_label, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(close_btn, wifi_popup_close_cb, LV_EVENT_CLICKED, popup);   // Auto-delete popup when close button is clicked
            lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);                                  // Also make popup clickable to close (but not the children)

        } else if (g_wifi_state == WIFI_STATE_DISCONNECTED) {                               // Request WiFi reconnection
            ESP_LOGI(TAG, "Requesting WiFi reconnection");
            g_wifi_reconnect_requested = true;

            // Set connecting state immediately for visual feedback
            g_wifi_state = WIFI_STATE_CONNECTING;
            if (g_wifi_icon) {
                lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_CONNECTING_COLOR), 0);
            }
        }
        // If CONNECTING, do nothing (non-clickable state)
    }
} /**/


/**
 * Rebuilding header bar
 */
static void rebuild_header_bar(void) {
    // Delete existing header bar if it exists
    if (g_header_bar) {
        lv_obj_del(g_header_bar);
        g_header_bar = NULL;
        g_hamburger_btn = NULL;
        g_gear_btn = NULL;
        g_wifi_icon = NULL;
    }
    // Delete existing profile list if it exists
    if (g_profile_list) {
        lv_obj_del(g_profile_list);
        g_profile_list = NULL;
    }
    // Create header bar container
    lv_obj_t *scr = lv_disp_get_scr_act(g_disp);
    g_header_bar = lv_obj_create(scr);
    lv_obj_set_size(g_header_bar, g_current_width, 40);
    lv_obj_align(g_header_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(g_header_bar, lv_color_hex(TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_header_bar, 0, 0);
    lv_obj_set_style_radius(g_header_bar, 0, 0);
    lv_obj_set_style_pad_all(g_header_bar, 0, 0);
    lv_obj_clear_flag(g_header_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Hamburger button (left side) - rotation
    g_hamburger_btn = lv_btn_create(g_header_bar);
    lv_obj_set_size(g_hamburger_btn, 40, 40);
    lv_obj_align(g_hamburger_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_hamburger_btn, lv_color_hex(TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_hamburger_btn, 0, 0);
    lv_obj_set_style_shadow_width(g_hamburger_btn, 0, 0);
    lv_obj_add_event_cb(g_hamburger_btn, hamburger_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hamburger_label = lv_label_create(g_hamburger_btn);
    lv_label_set_text(hamburger_label, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(hamburger_label, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_center(hamburger_label);
    lv_obj_clear_flag(hamburger_label, LV_OBJ_FLAG_CLICKABLE);

    // Title label (centered in landscape, left-aligned in portrait)
    lv_obj_t *title = lv_label_create(g_header_bar);
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);
    if (g_is_portrait) {
        // Portrait mode: shorter title and align next to hamburger button
        lv_label_set_text(title, "Keymaker");
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 45, 0);
    } else {
        // Landscape mode: full title and center it
        lv_label_set_text(title, "The Keymaker");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    }

    // WiFi icon button (right side, before gear button)
    lv_obj_t *wifi_btn = lv_btn_create(g_header_bar);
    lv_obj_set_size(wifi_btn, 40, 40);
    lv_obj_align(wifi_btn, LV_ALIGN_RIGHT_MID, -45, 0);  // 5px gap from gear button in both orientations
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(wifi_btn, 0, 0);
    lv_obj_set_style_shadow_width(wifi_btn, 0, 0);
    lv_obj_set_style_pad_all(wifi_btn, 0, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_icon_event_cb, LV_EVENT_CLICKED, NULL);

    // WiFi icon label inside button
    g_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(g_wifi_icon, LV_SYMBOL_WIFI);
    // Set WiFi icon color based on current connection status
    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_ACTIVE_COLOR), 0);  // Green when connected
    } else if (g_wifi_state == WIFI_STATE_CONNECTING) {
        lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_CONNECTING_COLOR), 0);  // Yellow when connecting
    } else {
        lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_INACTIVE_COLOR), 0);  // Gray when disconnected
    }
    lv_obj_set_style_text_font(g_wifi_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(g_wifi_icon);
    lv_obj_clear_flag(g_wifi_icon, LV_OBJ_FLAG_CLICKABLE);

    // Gear button (right side) - setup
    g_gear_btn = lv_btn_create(g_header_bar);
    lv_obj_set_size(g_gear_btn, 40, 40);
    lv_obj_align(g_gear_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_gear_btn, lv_color_hex(TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_gear_btn, 0, 0);
    lv_obj_set_style_shadow_width(g_gear_btn, 0, 0);
    lv_obj_set_style_pad_all(g_gear_btn, 0, 0);  // Remove padding for full clickable area
    lv_obj_add_event_cb(g_gear_btn, gear_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gear_label = lv_label_create(g_gear_btn);
    lv_label_set_text(gear_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gear_label, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_center(gear_label);
    lv_obj_clear_flag(gear_label, LV_OBJ_FLAG_CLICKABLE);
} /**/


/**
 * Tear down the OTP code display and show the label/issuer text again
 */
static void card_hide_otp_code(card_state_t *state) {
    if (state->otp_label) {
        lv_obj_del(state->otp_label);
        state->otp_label = NULL;
    }
    if (state->progress_bg) {
        lv_obj_del(state->progress_bg);
        state->progress_bg = NULL;
    }
    if (state->timer) {
        lv_timer_del(state->timer);
        state->timer = NULL;
    }
    // Show text container again
    if (state->text_container) {
        lv_obj_clear_flag(state->text_container, LV_OBJ_FLAG_HIDDEN);
    }
    state->showing_code = false;
} /**/
