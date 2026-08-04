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

#include "display_setup.h"
#include "display_main.h"
#include "portal.h"
#include "esp_log.h"

static const char *TAG = "display_setup";

// UI elements
static lv_obj_t *g_setup_screen = NULL;
static lv_obj_t *g_qrcode = NULL;
static lv_obj_t *g_cancel_btn = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_instruction_label = NULL;

// Display reference and original screen
static lv_disp_t *g_disp = NULL;
static lv_obj_t *g_original_screen = NULL;
static bool g_is_portrait = false;

// Forward declarations

/**
 * 
 */
static void cancel_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "Cancel button pressed - exiting setup");
        display_setup_hide();
    }
} /**/


/**
 * Showing setup screen
 */
void display_setup_show(lv_disp_t *disp) {
    ESP_LOGI(TAG, "Showing setup screen");
    // Store the current screen so we can restore it on cancel
    g_disp = disp;
    g_original_screen = lv_disp_get_scr_act(disp);

    // Detect current rotation to determine layout
    lv_disp_rot_t rotation = lv_disp_get_rotation(disp);
    g_is_portrait = (rotation == LV_DISP_ROT_270 || rotation == LV_DISP_ROT_90);
    int screen_width = g_is_portrait ? 240 : 320;
    int screen_height = g_is_portrait ? 320 : 240;
    ESP_LOGI(TAG, "Setup screen: portrait=%d, size=%dx%d", g_is_portrait, screen_width, screen_height);

    // Create new screen for setup
    g_setup_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_setup_screen, lv_color_hex(SCREEN_BACKGROUND_COLOR), 0);

    // Header bar (same style as main screen)
    lv_obj_t *header = lv_obj_create(g_setup_screen);
    lv_obj_set_size(header, screen_width, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Setup Mode");
    lv_obj_set_style_text_color(title, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // QR Code (will be created and updated with WiFi info), using a container to hold the QR code widget
    g_qrcode = lv_obj_create(g_setup_screen);
    lv_obj_set_size(g_qrcode, 160, 160);
    if (g_is_portrait) {
        // Portrait: center QR code horizontally, move 10px down to avoid covering text
        lv_obj_align(g_qrcode, LV_ALIGN_CENTER, 0, 30);
    } else {
        // Landscape: bottom left as before
        lv_obj_align(g_qrcode, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    }
    lv_obj_set_style_bg_color(g_qrcode, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_qrcode, 0, 0);
    lv_obj_set_style_radius(g_qrcode, 0, 0);
    lv_obj_set_style_pad_all(g_qrcode, 5, 0);

    // Cancel button
    g_cancel_btn = lv_btn_create(g_setup_screen);
    if (g_is_portrait) {
        // Portrait: bigger button at bottom (full width minus margins)
        lv_obj_set_size(g_cancel_btn, screen_width - 20, 40);
        lv_obj_align(g_cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
    } else {
        // Landscape: regular size
        lv_obj_set_size(g_cancel_btn, 120, 40);
        lv_obj_align(g_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    }
    lv_obj_set_style_pad_all(g_cancel_btn, 0, 0);  // Remove padding for full clickable area
    lv_obj_add_event_cb(g_cancel_btn, cancel_btn_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *cancel_label = lv_label_create(g_cancel_btn);
    lv_label_set_text(cancel_label, "Close");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_label);
    lv_obj_clear_flag(cancel_label, LV_OBJ_FLAG_CLICKABLE);

    // Status label
    g_status_label = lv_label_create(g_setup_screen);
    lv_label_set_text(g_status_label, "Initializing Captive Portal");  // Single line for portrait
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(g_status_label, g_is_portrait ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_WRAP);
    if (g_is_portrait) {
        // Portrait: top left below header, single line
        lv_obj_set_width(g_status_label, screen_width - 10);
        lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 5, 45);
    } else {
        // Landscape: right side as before
        lv_obj_set_width(g_status_label, 145);
        lv_obj_align(g_status_label, LV_ALIGN_TOP_RIGHT, -5, 75);
    }
    lv_obj_clear_flag(g_status_label, LV_OBJ_FLAG_CLICKABLE);

    // Instruction label
    g_instruction_label = lv_label_create(g_setup_screen);
    lv_label_set_text(g_instruction_label, "");  // Empty initially, will be set when portal is ready
    lv_obj_set_style_text_color(g_instruction_label, lv_color_hex(TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_text_font(g_instruction_label, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(g_instruction_label, LV_LABEL_LONG_WRAP);  // Always wrap
    if (g_is_portrait) {
        // Portrait: below status label
        lv_obj_set_width(g_instruction_label, screen_width - 10);
        lv_obj_align(g_instruction_label, LV_ALIGN_TOP_LEFT, 5, 70);
    } else {
        // Landscape: bottom right as before
        lv_obj_set_width(g_instruction_label, 145);
        lv_obj_align(g_instruction_label, LV_ALIGN_BOTTOM_RIGHT, -5, -60);
    }
    lv_obj_clear_flag(g_instruction_label, LV_OBJ_FLAG_CLICKABLE);

    // Load the setup screen
    lv_disp_load_scr(g_setup_screen);
    ESP_LOGI(TAG, "Setup screen loaded");
} /**/


/**
 * Hide setup screen
 */
void display_setup_hide(void) {
    ESP_LOGI(TAG, "Hiding setup screen");
    if (g_setup_screen) {
        // Restore the original main screen FIRST (instant feedback)
        if (g_original_screen) {
            lv_disp_load_scr(g_original_screen);
            ESP_LOGI(TAG, "Main screen restored");
        }

        // Delete setup screen
        lv_obj_del(g_setup_screen);
        g_setup_screen = NULL;
        g_qrcode = NULL;
        g_cancel_btn = NULL;
        g_status_label = NULL;
        g_instruction_label = NULL;
        g_original_screen = NULL;

        // Request portal exit to trigger WiFi reconnection in main loop
        portal_request_exit();
        // Stop the captive portal AFTER (runs in background with stopping flag set)
        portal_stop();
        ESP_LOGI(TAG, "Setup screen hidden, portal stopping in background");
    }
} /**/


/**
 * Create WiFi QR code data in standard format
 */
void display_setup_update_qrcode(const char *ssid, const char *url) {
    if (!g_qrcode || !ssid || !url) {
        ESP_LOGW(TAG, "Cannot update QR code: qrcode=%p, ssid=%p, url=%p", g_qrcode, ssid, url);
        return;
    }
    // Create WiFi QR code data in standard format, this allows phones to scan and connect to the WiFi network directly
    //      Format: WIFI:T:nopass;S:<ssid>;P:;;
    char qr_data[256];
    snprintf(qr_data, sizeof(qr_data), "WIFI:T:nopass;S:%s;P:;;", ssid);
    ESP_LOGI(TAG, "Creating QR code with data: %s", qr_data);

    // Clear any existing children
    lv_obj_clean(g_qrcode);
    // Create LVGL QR code widget (black on white for best scanning)
    lv_obj_t *qr = lv_qrcode_create(g_qrcode, 150, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    // Update QR code with WiFi connection data
    lv_qrcode_update(qr, qr_data, strlen(qr_data));
    // Center the QR code in the container
    lv_obj_center(qr);
    ESP_LOGI(TAG, "QR code updated successfully for SSID=%s, URL=%s", ssid, url);
} /**/


/**
 * Detect if setup screen is visible
 */
bool display_setup_is_visible(void) {
    return (g_setup_screen != NULL);
} /**/

