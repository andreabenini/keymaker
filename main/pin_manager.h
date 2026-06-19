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
#ifndef PIN_MANAGER_H
#define PIN_MANAGER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "crypto.h"


// Forward declarations (actual types defined in implementation)
typedef struct _lv_disp_t lv_disp_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;
typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;

/**
 * @brief Initialize PIN management system and unlock device
 *
 * This function handles both first-time PIN setup and normal unlock:
 * - First boot: Asks for PIN twice (confirmation), generates salt, creates verification
 * - Normal boot: Asks for PIN once, verifies against stored data, retries on wrong PIN
 *
 * Shows "Decrypting device..." message during key derivation (PBKDF2).
 *
 * @param disp LVGL display object
 * @param panel_handle LCD panel handle
 * @param touch_handle Touch controller handle
 * @param lvgl_mux Mutex for LVGL operations
 * @param key_out Output buffer for derived encryption key (CRYPTO_KEY_SIZE bytes)
 * @return ESP_OK on successful unlock, error code otherwise
 */
esp_err_t pin_manager_unlock(lv_disp_t *disp, esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle, SemaphoreHandle_t lvgl_mux, uint8_t *key_out);

#endif // PIN_MANAGER_H
