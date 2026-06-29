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
#ifndef CALIBRATION_H
#define CALIBRATION_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

// Touch calibration data structure
typedef struct {
    int16_t min_x;
    int16_t max_x;
    int16_t min_y;
    int16_t max_y;
    bool valid;
} touch_cal_data_t;


/**
 * @brief Set touch handle for calibration, Must be called before touch_cal_run()
 * @param handle ESP LCD touch handle
 */
void touch_cal_set_handle(esp_lcd_touch_handle_t handle);

/**
 * @brief Initialize touch calibration system, loads calibration from NVS if available
 * @return ESP_OK on success
 */
esp_err_t touch_cal_init(void);

/**
 * @brief Check if valid calibration data exists
 * @return true if calibration exists, false otherwise
 */
bool touch_cal_exists(void);

/**
 * @brief Show calibration screen and run calibration procedure
 *        User will be prompted to touch 4 corner points, Calibration data is automatically saved to NVS
 * @param disp LVGL display handle
 * @return ESP_OK on success
 */
esp_err_t touch_cal_run(lv_disp_t *disp);

/**
 * @brief Transform raw touch coordinates to calibrated screen coordinates
 * @param raw_x Raw X coordinate from touch controller
 * @param raw_y Raw Y coordinate from touch controller
 * @param cal_x Output calibrated X coordinate
 * @param cal_y Output calibrated Y coordinate
 */
void touch_cal_transform(int16_t raw_x, int16_t raw_y, int16_t *cal_x, int16_t *cal_y);

/**
 * @brief Clear calibration data from NVS, Use this to force recalibration on next boot
 * @return ESP_OK on success
 */
esp_err_t touch_cal_clear(void);


#endif
