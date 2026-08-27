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


/**
 * Progress bar timer callback - updates progress and hides code when expired
 */
static void progress_timer_cb(lv_timer_t *timer) {
    card_state_t *state = (card_state_t *)timer->user_data;
    if (!state || !state->showing_code) {
        return;
    }
    // Get current time and calculate remaining seconds
    uint64_t current_time = time_sync_get_unix_time();
    if (current_time == 0) {
        // Time sync lost, hide the code
        ESP_LOGW(TAG, "Time sync lost (get_unix_time returned 0), hiding OTP code");
        card_hide_otp_code(state);
        return;
    }
    // Check if we've passed the window end time
    if (current_time >= state->window_end) {
        // Code expired, hide it
        ESP_LOGI(TAG, "OTP code expired: current=%llu, window_end=%llu", current_time, state->window_end);
        card_hide_otp_code(state);
        return;
    }
    // Update black background width based on time remaining
    uint32_t remaining = state->window_end - current_time;
    uint32_t window_duration = state->window_end - state->window_start;
    int card_width = lv_obj_get_width(state->card);
    int bg_width = (card_width - 50) * remaining / window_duration;  // Minus icon width
    lv_obj_set_width(state->progress_bg, bg_width);
    // Log every second for debugging
    static uint64_t last_log_time = 0;
    if (current_time != last_log_time) {
        ESP_LOGI(TAG, "Progress: remaining=%lu sec, bg_width=%d", remaining, bg_width);
        last_log_time = current_time;
    }
} /**/


/**
 * Card tap event handler - shows OTP code or WiFi error
 */
static void card_tap_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    card_state_t *state = (card_state_t *)lv_event_get_user_data(e);
    if (!state) {
        ESP_LOGE(TAG, "Card tap: no state data");
        return;
    }
    ESP_LOGI(TAG, "Card tapped: profile index %d (showing_code=%d)", state->profile_index, state->showing_code);

    // already showing code, clean up and restart with fresh code
    if (state->showing_code) {
        ESP_LOGI(TAG, "Card already showing code, restarting with fresh code");
        card_hide_otp_code(state);
    }

    // Check if time is synchronized
    bool is_synced = time_sync_is_synchronized();
    ESP_LOGI(TAG, "Time sync check: %s", is_synced ? "SYNCED" : "NOT SYNCED");
    if (!is_synced) {
        ESP_LOGW(TAG, "Time not synchronized, showing error");
        wifi_error_show(g_disp);
        return;
    }
    ESP_LOGI(TAG, "Time is synchronized, generating OTP code");

    // Load profile from config
    keymaker_config_t *config = malloc(sizeof(keymaker_config_t));
    if (!config) {
        ESP_LOGE(TAG, "Failed to allocate memory for config");
        return;
    }
    if (config_load(config) != ESP_OK || state->profile_index >= config->profile_count) {
        ESP_LOGE(TAG, "Failed to load profile");
        free(config);
        return;
    }
    otp_profile_t *profile = &config->profiles[state->profile_index];

    // Generate TOTP code
    char code_str[16];
    uint64_t current_time = time_sync_get_unix_time();
    if (!totp_generate(profile, current_time, code_str)) {
        ESP_LOGE(TAG, "Failed to generate TOTP code");
        free(config);
        return;
    }
    ESP_LOGI(TAG, "Generated code: %s", code_str);

    // Hide label/issuer
    if (state->text_container) {
        lv_obj_add_flag(state->text_container, LV_OBJ_FLAG_HIDDEN);
    }

    // Calculate TOTP window boundaries FIRST, Time counter is which 30-second window we're in
    uint64_t time_counter = current_time / profile->period;
    state->window_start = time_counter * profile->period;
    state->window_end = (time_counter + 1) * profile->period;
    state->period = profile->period;

    // Calculate initial width based on remaining time
    uint32_t remaining = state->window_end - current_time;
    uint32_t window_duration = state->window_end - state->window_start;
    int card_width = lv_obj_get_width(state->card);
    int card_height = lv_obj_get_height(state->card);
    int initial_bg_width = (card_width - 50) * remaining / window_duration;  // Minus icon width
    ESP_LOGI(TAG, "TOTP window: %llu to %llu (current: %llu, remaining: %llu sec, initial_width: %d)", state->window_start, state->window_end, current_time, remaining, initial_bg_width);

    // Create black background that shrinks as time progresses
    state->progress_bg = lv_obj_create(state->card);
    lv_obj_set_size(state->progress_bg, initial_bg_width, card_height);  // Set correct initial width
    lv_obj_align(state->progress_bg, LV_ALIGN_LEFT_MID, 50, 0);  // Starts where OTP text begins
    lv_obj_set_style_bg_color(state->progress_bg, lv_color_hex(OTP_PROGRESS_BG_COLOR), 0);
    lv_obj_set_style_border_width(state->progress_bg, 0, 0);
    lv_obj_set_style_radius(state->progress_bg, 0, 0);
    lv_obj_set_style_pad_all(state->progress_bg, 0, 0);
    lv_obj_clear_flag(state->progress_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(state->progress_bg, LV_OBJ_FLAG_SCROLLABLE);

    // Create OTP code label on top of black background
    state->otp_label = lv_label_create(state->card);
    lv_label_set_text(state->otp_label, code_str);
    lv_obj_set_style_text_color(state->otp_label, lv_color_hex(OTP_CODE_COLOR), 0);
    lv_obj_set_style_text_font(state->otp_label, &lv_font_montserrat_48, 0);
    lv_obj_align(state->otp_label, LV_ALIGN_LEFT_MID, 50, 0);  // Offset for icon
    lv_obj_clear_flag(state->otp_label, LV_OBJ_FLAG_CLICKABLE);
    state->showing_code = true;

    // Create timer for progress bar updates (100ms interval)
    state->timer = lv_timer_create(progress_timer_cb, 100, state);
    free(config);
} /**/


/**
 * Building and rebuilding profile list on the main screen
 */
static void rebuild_profile_list(void) {
    // Clean up any active card states (timers, etc)
    for (int i = 0; i < g_card_state_count; i++) {
        if (g_card_states[i].timer) {
            lv_timer_del(g_card_states[i].timer);
            g_card_states[i].timer = NULL;
        }
    }
    g_card_state_count = 0;

    // Delete existing profile list if it exists
    if (g_profile_list) {
        lv_obj_del(g_profile_list);
        g_profile_list = NULL;
    }
    lv_obj_t *scr = lv_disp_get_scr_act(g_disp);

    // Create scrollable container for profile cards
    g_profile_list = lv_obj_create(scr);
    lv_obj_set_size(g_profile_list, g_current_width, g_current_height - 40);        // Full width, height minus header
    lv_obj_align(g_profile_list, LV_ALIGN_TOP_MID, 0, 40);                          // Position below header
    lv_obj_set_style_bg_color(g_profile_list, lv_color_hex(SCREEN_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_profile_list, 0, 0);
    lv_obj_set_style_radius(g_profile_list, 0, 0);
    lv_obj_set_style_pad_all(g_profile_list, 10, 0);
    lv_obj_set_style_pad_right(g_profile_list, 0, 0);                               // No right padding for scrollbar alignment
    // Scrollbar styling - align to right edge with no padding
    lv_obj_set_style_pad_right(g_profile_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_scrollbar_mode(g_profile_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(g_profile_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_profile_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    // Load profiles from config (allocate on heap to avoid stack overflow)
    keymaker_config_t *config = malloc(sizeof(keymaker_config_t));
    if (!config) {
        ESP_LOGE(TAG, "Failed to allocate memory for config");
        return;
    }
    esp_err_t ret = config_load(config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No profiles to display (config not found or empty)");
        // Show "No profiles" message
        lv_obj_t *no_profiles_label = lv_label_create(g_profile_list);
        lv_label_set_text(no_profiles_label, "No profiles yet\n\nTap " LV_SYMBOL_SETTINGS " to add profiles");
        lv_obj_set_style_text_color(no_profiles_label, lv_color_hex(CARD_TEXT_SECONDARY_COLOR), 0);
        lv_obj_set_style_text_align(no_profiles_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(no_profiles_label);
        free(config);
        return;
    }
    ESP_LOGI(TAG, "Displaying %d profiles", config->profile_count);

    // Create a card for each profile
    for (int i = 0; i < config->profile_count; i++) {
        // Create card container
        otp_profile_t *profile = &config->profiles[i];
        lv_obj_t *card = lv_obj_create(g_profile_list);
        lv_obj_set_size(card, g_current_width - 22, 54);            // Leave room for scrollbar
        lv_obj_set_x(card, 0);                                      // Flush left to gain space
        lv_obj_set_style_bg_color(card, lv_color_hex(CARD_BACKGROUND_COLOR), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_pad_all(card, 5, 0);                       // Compact padding
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        // Create icon circle (left side)
        lv_obj_t *icon = lv_obj_create(card);
        lv_obj_set_size(icon, 40, 40);                              // icon size
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 3, 0);                // 3px from left edge
        lv_obj_set_style_bg_color(icon, lv_color_hex(CARD_ICON_BG_COLOR), 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_style_radius(icon, 20, 0);                       // Make it circular
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        // Use profile icon field
        lv_obj_t *icon_label = lv_label_create(icon);
        lv_label_set_text(icon_label, profile->icon);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(CARD_ICON_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_20, 0);  // Font
        lv_obj_center(icon_label);
        lv_obj_clear_flag(icon_label, LV_OBJ_FLAG_CLICKABLE);
        // Create text container (right side)
        lv_obj_t *text_container = lv_obj_create(card);
        lv_obj_set_size(text_container, g_current_width - 75, 40);  // Adjusted width
        lv_obj_align(text_container, LV_ALIGN_LEFT_MID, 53, 0);     // Card spacing
        lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_container, 0, 0);
        lv_obj_set_style_pad_all(text_container, 0, 0);
        lv_obj_clear_flag(text_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(text_container, LV_OBJ_FLAG_CLICKABLE);
        // Label (Line 1, always shown)
        lv_obj_t *label_text = lv_label_create(text_container);
        lv_label_set_text(label_text, profile->label);
        lv_obj_set_style_text_color(label_text, lv_color_hex(CARD_TEXT_COLOR), 0);
        lv_obj_set_style_text_font(label_text, &lv_font_montserrat_16, 0);  // Readable font
        lv_obj_align(label_text, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_clear_flag(label_text, LV_OBJ_FLAG_CLICKABLE);
        lv_label_set_long_mode(label_text, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label_text, g_current_width - 75);
        // Issuer (Line 2, smaller, only if present)
        if (profile->issuer[0] != '\0') {
            lv_obj_t *issuer_text = lv_label_create(text_container);
            lv_label_set_text(issuer_text, profile->issuer);
            lv_obj_set_style_text_color(issuer_text, lv_color_hex(CARD_TEXT_SECONDARY_COLOR), 0);
            lv_obj_set_style_text_font(issuer_text, &lv_font_montserrat_16, 0);     // Issuer font
            lv_obj_align(issuer_text, LV_ALIGN_TOP_LEFT, 0, 17);                    // Tighter to the border
            lv_obj_clear_flag(issuer_text, LV_OBJ_FLAG_CLICKABLE);
            lv_label_set_long_mode(issuer_text, LV_LABEL_LONG_DOT);
            lv_obj_set_width(issuer_text, g_current_width - 75);
        }
        // Initialize card state and add tap handler
        if (i < MAX_CARD_STATES) {
            card_state_t *state = &g_card_states[i];
            state->profile_index = i;
            state->card = card;
            state->text_container = text_container;
            state->otp_label = NULL;
            state->progress_bg = NULL;
            state->timer = NULL;
            state->showing_code = false;

            // Add tap event to card
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, card_tap_event_cb, LV_EVENT_CLICKED, state);

            g_card_state_count = i + 1;
        }
    }
    // Free config memory
    free(config);
} /**/


/**
 * Create main screen display
 */
void display_main_create(lv_disp_t *disp, esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle) {
    // Store display, panel, and touch handles for rotation
    ESP_LOGI(TAG, "DEBUG: display_main_create()");
    g_disp = disp;
    g_panel_handle = panel_handle;
    g_touch_handle = touch_handle;

    // Detect current rotation from display (in case we're coming from PIN screen in portrait)
    lv_disp_rot_t rotation = lv_disp_get_rotation(disp);
    if (rotation == LV_DISP_ROT_270 || rotation == LV_DISP_ROT_90) {
        // Portrait mode
        g_is_portrait = true;
        g_current_width = LCD_V_RES;
        g_current_height = LCD_H_RES;
        ESP_LOGI(TAG, "Main screen created in portrait mode (rotation=%d)", rotation);
    } else {
        // Landscape mode
        g_is_portrait = false;
        g_current_width = LCD_H_RES;
        g_current_height = LCD_V_RES;
        ESP_LOGI(TAG, "Main screen created in landscape mode (rotation=%d)", rotation);
    }
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    ESP_LOGI(TAG, "DEBUG: Active screen in display_main_create: %p", scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(SCREEN_BACKGROUND_COLOR), 0);       // Set background to dark gray
    ESP_LOGI(TAG, "DEBUG: Building header bar...");                                 // Build the initial header bar
    rebuild_header_bar();
    ESP_LOGI(TAG, "DEBUG: Building profile list...");                               // Build the initial profile list
    rebuild_profile_list();
    ESP_LOGI(TAG, "DEBUG: display_main_create() completed");
} /**/


/**
 * Setting wifi status
 */
void display_main_set_wifi_status(bool connected) {
    // Update global WiFi state
    g_wifi_connected = connected;
    g_wifi_state     = connected ? WIFI_STATE_CONNECTED : WIFI_STATE_DISCONNECTED;
    // Update WiFi icon if it exists
    if (g_wifi_icon) {
        if (connected) {
            lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_ACTIVE_COLOR), 0);   // Bright green when connected
        } else {
            lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_INACTIVE_COLOR), 0); // Gray when disconnected
        }
    }
} /**/


/**
 * Coloring wifi icon while connecting
 */
void display_main_set_wifi_connecting(void) {
    g_wifi_state = WIFI_STATE_CONNECTING;
    g_wifi_connected = false;
    // Update WiFi icon if it exists
    if (g_wifi_icon) {
        lv_obj_set_style_text_color(g_wifi_icon, lv_color_hex(WIFI_CONNECTING_COLOR), 0);  // Yellow when connecting
    }
} /**/


/**
 * Setting up main wifi information
 */
void display_main_set_wifi_info(const char *ssid, const char *ip_addr, int8_t rssi) {
    if (ssid) {
        strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid) - 1);
        g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
    }
    if (ip_addr) {
        strncpy(g_wifi_ip, ip_addr, sizeof(g_wifi_ip) - 1);
        g_wifi_ip[sizeof(g_wifi_ip) - 1] = '\0';
    }
    g_wifi_rssi = rssi;
} /**/
