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
#include "touch_calibration.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>


static const char *TAG = "touch_cal";


// NVS storage
#define NVS_NAMESPACE "touch_cal"
#define NVS_KEY_MIN_X "min_x"
#define NVS_KEY_MAX_X "max_x"
#define NVS_KEY_MIN_Y "min_y"
#define NVS_KEY_MAX_Y "max_y"
#define NVS_KEY_VALID "valid"

// Screen dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// Calibration target positions (screen coordinates)
#define CAL_MARGIN 15
static const struct {
    int16_t x;
    int16_t y;
} cal_targets[4] = {
    {CAL_MARGIN, CAL_MARGIN},                                   // Top left
    {SCREEN_WIDTH - CAL_MARGIN, CAL_MARGIN},                    // Top right
    {SCREEN_WIDTH - CAL_MARGIN, SCREEN_HEIGHT - CAL_MARGIN},    // Bottom right
    {CAL_MARGIN, SCREEN_HEIGHT - CAL_MARGIN}                    // Bottom left
};

// Global calibration data
static touch_cal_data_t g_cal_data = {0};

// External touch handle (needs to be set by main.c)
static esp_lcd_touch_handle_t g_touch_handle = NULL;

// Forward declarations
static esp_err_t load_calibration(void);
static esp_err_t save_calibration(void);


/**
 * @brief Initialization of the touch screen window, loading calibration values
 */
esp_err_t touch_cal_init(void) {
    ESP_LOGI(TAG, "Initializing touch calibration");
    // Load calibration from NVS
    esp_err_t ret = load_calibration();
    if (ret == ESP_OK && g_cal_data.valid) {
        ESP_LOGI(TAG, "Calibration loaded: X[%d,%d] Y[%d,%d]",
                 g_cal_data.min_x, g_cal_data.max_x,
                 g_cal_data.min_y, g_cal_data.max_y);
    } else {
        ESP_LOGW(TAG, "No valid calibration found");
        g_cal_data.valid = false;
    }
    return ESP_OK;
} /**/


/**
 * @brief property reading touch screen data calibration values
 */
bool touch_cal_exists(void) {
    return g_cal_data.valid;
} /**/


void touch_cal_set_handle(esp_lcd_touch_handle_t handle) {
    g_touch_handle = handle;
} /**/


/**
 * @brief Loading calibration values from NVS storage
 */
static esp_err_t load_calibration(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    // Load all calibration values
    int16_t min_x, max_x, min_y, max_y;
    uint8_t valid;
    ret  = nvs_get_i16(nvs_handle, NVS_KEY_MIN_X, &min_x);
    ret |= nvs_get_i16(nvs_handle, NVS_KEY_MAX_X, &max_x);
    ret |= nvs_get_i16(nvs_handle, NVS_KEY_MIN_Y, &min_y);
    ret |= nvs_get_i16(nvs_handle, NVS_KEY_MAX_Y, &max_y);
    ret |= nvs_get_u8(nvs_handle,  NVS_KEY_VALID, &valid);
    nvs_close(nvs_handle);
    if (ret == ESP_OK && valid) {
        g_cal_data.min_x = min_x;
        g_cal_data.max_x = max_x;
        g_cal_data.min_y = min_y;
        g_cal_data.max_y = max_y;
        g_cal_data.valid = true;
        return ESP_OK;
    }
    return ESP_FAIL;
} /**/


/**
 * @brief Save calibration data to NVS storage memory
 */
static esp_err_t save_calibration(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    // Save all calibration values
    ret = nvs_set_i16(nvs_handle, NVS_KEY_MIN_X, g_cal_data.min_x);
    ret |= nvs_set_i16(nvs_handle, NVS_KEY_MAX_X, g_cal_data.max_x);
    ret |= nvs_set_i16(nvs_handle, NVS_KEY_MIN_Y, g_cal_data.min_y);
    ret |= nvs_set_i16(nvs_handle, NVS_KEY_MAX_Y, g_cal_data.max_y);
    ret |= nvs_set_u8(nvs_handle, NVS_KEY_VALID, 1);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved: X[%d,%d] Y[%d,%d]", g_cal_data.min_x, g_cal_data.max_x, g_cal_data.min_y, g_cal_data.max_y);
    } else {
        ESP_LOGE(TAG, "Failed to save calibration: %s", esp_err_to_name(ret));
    }
    return ret;
} /**/


/**
 * @brief Touch screen calibration function
 */
esp_err_t touch_cal_run(lv_disp_t *disp) {
    if (!g_touch_handle) {
        ESP_LOGE(TAG, "Touch handle not set! Call touch_cal_set_handle() first");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Starting touch calibration");
    // Clear existing calibration data from NVS
    ESP_LOGI(TAG, "Clearing existing calibration data");
    touch_cal_clear();

    // Reset touch controller to landscape defaults (same as boot settings)
    // This ensures calibration is always done with known, consistent settings
    ESP_LOGI(TAG, "Resetting touch controller to landscape defaults");
    esp_lcd_touch_set_swap_xy(g_touch_handle, true);
    esp_lcd_touch_set_mirror_x(g_touch_handle, true);
    esp_lcd_touch_set_mirror_y(g_touch_handle, true);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // Clear screen
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Create title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Touch Calibration");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Create instruction label
    lv_obj_t *instr = lv_label_create(scr);
    lv_label_set_text(instr, "Touch the center\nof each target");
    lv_obj_set_style_text_color(instr, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(instr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, -50);

    // Arrays to store raw touch readings
    int16_t raw_x[4] = {0};
    int16_t raw_y[4] = {0};

    // Calibrate each corner (Main calibration loop)
    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG, "Calibrating point %d at screen (%d, %d)", i, cal_targets[i].x, cal_targets[i].y);
        // Create target circle
        lv_obj_t *target = lv_obj_create(scr);
        lv_obj_set_size(target, 40, 40);
        lv_obj_set_pos(target, cal_targets[i].x - 20, cal_targets[i].y - 20);
        lv_obj_set_style_bg_color(target, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(target, 2, 0);
        lv_obj_set_style_border_color(target, lv_color_hex(0xFFFFFF), 0);
        // Create center dot
        lv_obj_t *center = lv_obj_create(target);
        lv_obj_set_size(center, 8, 8);
        lv_obj_center(center);
        lv_obj_set_style_bg_color(center, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(center, 0, 0);
        // Update display
        lv_refr_now(disp);
        // Wait for touch
        ESP_LOGI(TAG, "Waiting for touch...");
        bool touched = false;
        bool was_touched = false;
        while (!touched) {
            uint16_t touch_x[1];
            uint16_t touch_y[1];
            uint16_t touch_strength[1];
            uint8_t touch_cnt = 0;
            esp_lcd_touch_read_data(g_touch_handle);
            bool is_touched = esp_lcd_touch_get_coordinates(g_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
            if (is_touched && touch_cnt > 0 && !was_touched) {
                // New touch detected
                raw_x[i] = touch_x[0];
                raw_y[i] = touch_y[0];
                ESP_LOGI(TAG, "Touch detected: raw(%d, %d)", raw_x[i], raw_y[i]);

                // Flash the target to confirm
                lv_obj_set_style_bg_color(target, lv_color_hex(0x00FF00), 0);
                lv_refr_now(disp);
                vTaskDelay(pdMS_TO_TICKS(200));
                touched = true;
            }
            was_touched = is_touched;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        // Wait for release
        while (true) {
            uint16_t touch_x[1];
            uint16_t touch_y[1];
            uint16_t touch_strength[1];
            uint8_t touch_cnt = 0;
            esp_lcd_touch_read_data(g_touch_handle);
            bool is_touched = esp_lcd_touch_get_coordinates(g_touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);
            if (!is_touched) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        // Remove target
        lv_obj_del(target);
        lv_refr_now(disp);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    // Log all raw readings
    ESP_LOGI(TAG, "Raw readings collected:");
    ESP_LOGI(TAG, "  Point 0 (top-left     %d,%d): raw(%d, %d)", cal_targets[0].x, cal_targets[0].y, raw_x[0], raw_y[0]);
    ESP_LOGI(TAG, "  Point 1 (top-right    %d,%d): raw(%d, %d)", cal_targets[1].x, cal_targets[1].y, raw_x[1], raw_y[1]);
    ESP_LOGI(TAG, "  Point 2 (bottom-right %d,%d): raw(%d, %d)", cal_targets[2].x, cal_targets[2].y, raw_x[2], raw_y[2]);
    ESP_LOGI(TAG, "  Point 3 (bottom-left  %d,%d): raw(%d, %d)", cal_targets[3].x, cal_targets[3].y, raw_x[3], raw_y[3]);

    // Calculate calibration bounds
    g_cal_data.min_x = (raw_x[0] + raw_x[3]) / 2;  // Average of left points
    g_cal_data.max_x = (raw_x[1] + raw_x[2]) / 2;  // Average of right points
    g_cal_data.min_y = (raw_y[0] + raw_y[1]) / 2;  // Average of top points
    g_cal_data.max_y = (raw_y[2] + raw_y[3]) / 2;  // Average of bottom points
    g_cal_data.valid = true;

    ESP_LOGI(TAG, "Calibration bounds calculated:");
    ESP_LOGI(TAG, "  min_x = (%d + %d) / 2 = %d (left edge, maps to screen x=0)", raw_x[0], raw_x[3], g_cal_data.min_x);
    ESP_LOGI(TAG, "  max_x = (%d + %d) / 2 = %d (right edge, maps to screen x=%d)", raw_x[1], raw_x[2], g_cal_data.max_x, SCREEN_WIDTH-1);
    ESP_LOGI(TAG, "  min_y = (%d + %d) / 2 = %d (top edge, maps to screen y=0)", raw_y[0], raw_y[1], g_cal_data.min_y);
    ESP_LOGI(TAG, "  max_y = (%d + %d) / 2 = %d (bottom edge, maps to screen y=%d)", raw_y[2], raw_y[3], g_cal_data.max_y, SCREEN_HEIGHT-1);
    ESP_LOGI(TAG, "Calibration complete: X[%d,%d] Y[%d,%d]", g_cal_data.min_x, g_cal_data.max_x, g_cal_data.min_y, g_cal_data.max_y);

    // Save to NVS
    esp_err_t ret = save_calibration();

    // Show success message
    lv_obj_clean(scr);
    lv_obj_t *success = lv_label_create(scr);
    lv_label_set_text(success, "Calibration\nComplete!");
    lv_obj_set_style_text_color(success, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_align(success, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(success, LV_ALIGN_CENTER, 0, 0);
    lv_refr_now(disp);
    vTaskDelay(pdMS_TO_TICKS(1500));
    return ret;
} /**/


/**
 * @brief Touch screen calibration transformation (linear interpolation)
 */
void touch_cal_transform(int16_t raw_x, int16_t raw_y, int16_t *cal_x, int16_t *cal_y) {
    // No calibration - pass through
    if (!g_cal_data.valid) {
        *cal_x = raw_x;
        *cal_y = raw_y;
        return;
    }
    // Linear interpolation: map [min, max] raw to [0, screen_size]
    int32_t x = ((int32_t)(raw_x - g_cal_data.min_x) * SCREEN_WIDTH)  / (g_cal_data.max_x - g_cal_data.min_x);
    int32_t y = ((int32_t)(raw_y - g_cal_data.min_y) * SCREEN_HEIGHT) / (g_cal_data.max_y - g_cal_data.min_y);
    // Store pre-clamp values for logging
    int32_t x_before_clamp = x;
    int32_t y_before_clamp = y;
    bool clamped = false;
    // Clamp to screen bounds
    if (x < 0) { x = 0; clamped = true; }
    if (x >= SCREEN_WIDTH) { x = SCREEN_WIDTH - 1; clamped = true; }
    if (y < 0) { y = 0; clamped = true; }
    if (y >= SCREEN_HEIGHT) { y = SCREEN_HEIGHT - 1; clamped = true; }
    *cal_x = (int16_t)x;
    *cal_y = (int16_t)y;

    // Log transformation details (helps debug calibration issues)
    if (clamped) {
        ESP_LOGD(TAG, "Transform: raw(%d,%d) -> calc(%d,%d) -> clamped(%d,%d) [bounds: X[%d,%d] Y[%d,%d]]",
                 raw_x, raw_y, x_before_clamp, y_before_clamp, *cal_x, *cal_y,
                 g_cal_data.min_x, g_cal_data.max_x, g_cal_data.min_y, g_cal_data.max_y);
    } else {
        ESP_LOGD(TAG, "Transform: raw(%d,%d) -> cal(%d,%d) [bounds: X[%d,%d] Y[%d,%d]]",
                 raw_x, raw_y, *cal_x, *cal_y,
                 g_cal_data.min_x, g_cal_data.max_x, g_cal_data.min_y, g_cal_data.max_y);
    }
} /**/


/**
 * @brief Clearing calibration data
 */
esp_err_t touch_cal_clear(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    nvs_erase_all(nvs_handle);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    g_cal_data.valid = false;
    ESP_LOGI(TAG, "Calibration cleared");
    return ESP_OK;
} /**/
