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
#include "wifi_error.h"
#include "esp_log.h"


static const char *TAG = "wifi_error";
static lv_obj_t *error_screen = NULL;


/**
 * Event handler to dismiss error screen on tap
 */
static void error_screen_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Error screen tapped, dismissing");
        wifi_error_hide();
    }
} /**/


/**
 * WiFi messagebox for displaying errors
 */
void wifi_error_show(lv_disp_t *disp) {
    if (error_screen) {
        ESP_LOGW(TAG, "Error screen already visible");
        return;
    }
    ESP_LOGI(TAG, "Showing WiFi error screen");
    // Get current screen
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    // Create error screen container (full screen overlay)
    error_screen = lv_obj_create(scr);
    lv_obj_set_size(error_screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(error_screen, 0, 0);
    lv_obj_set_style_bg_color(error_screen, lv_color_hex(WIFI_ERROR_BG_COLOR), 0);
    lv_obj_set_style_border_width(error_screen, 0, 0);
    lv_obj_set_style_pad_all(error_screen, 0, 0);
    lv_obj_clear_flag(error_screen, LV_OBJ_FLAG_SCROLLABLE);
    // Create error message
    lv_obj_t *msg = lv_label_create(error_screen);
    lv_label_set_text(msg, "Syncing time...\n\nPlease wait a moment\nand try again");
    lv_obj_set_style_text_color(msg, lv_color_hex(WIFI_ERROR_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(msg, LV_OBJ_FLAG_CLICKABLE);
    // Create hint text at bottom
    lv_obj_t *hint = lv_label_create(error_screen);
    lv_label_set_text(hint, "tap the screen to continue...");
    lv_obj_set_style_text_color(hint, lv_color_hex(WIFI_ERROR_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    // Make it clickable to dismiss
    lv_obj_add_flag(error_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(error_screen, error_screen_event_cb, LV_EVENT_CLICKED, NULL);
} /**/


/**
 * Close/hide the wifi error messagebox
 */
void wifi_error_hide(void) {
    if (!error_screen) {
        return;
    }
    ESP_LOGI(TAG, "Hiding WiFi error screen");
    lv_obj_del(error_screen);
    error_screen = NULL;
} /**/
