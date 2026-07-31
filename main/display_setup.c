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

