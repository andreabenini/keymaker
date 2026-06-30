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

#include "pin_manager.h"
#include "display_pin.h"
#include "crypto.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "pin_manager";

#define NVS_NAMESPACE "keymaker"
#define NVS_KEY_SALT "salt"
#define NVS_KEY_VERIFY "pin_verify"

// Maximum size for verification blob
#define MAX_VERIFY_SIZE 128

/**
 * @brief Check if this is first boot (no salt in NVS)
 */
static bool is_first_boot(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return true; // No NVS namespace = first boot
    }
    size_t salt_len = CRYPTO_SALT_SIZE;
    uint8_t salt[CRYPTO_SALT_SIZE];
    ret = nvs_get_blob(nvs_handle, NVS_KEY_SALT, salt, &salt_len);
    nvs_close(nvs_handle);
    return (ret != ESP_OK);
} /**/