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
