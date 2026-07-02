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


/**
 * @brief Show status message on screen
 */
static void show_status_message(lv_disp_t *disp, const char *message) {
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_refr_now(disp);
} /**/


/**
 * @brief Handle first boot PIN setup
 */
static esp_err_t handle_first_boot(lv_disp_t *disp, esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle, SemaphoreHandle_t lvgl_mux, uint8_t *key_out) {
    ESP_LOGI(TAG, "First boot detected - setting up PIN");
    char pin1[PIN_MAX_LENGTH + 1];
    char pin2[PIN_MAX_LENGTH + 1];

    // Ask for PIN (first time)
    if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
        display_pin_create(disp, panel_handle, touch_handle);
        xSemaphoreGive(lvgl_mux);
    }
    while (1) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            bool complete = display_pin_is_complete(pin1);
            xSemaphoreGive(lvgl_mux);
            if (complete) break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "First PIN entered (length: %d)", strlen(pin1));

    // Ask for PIN confirmation
    if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
        display_pin_hide();
        show_status_message(disp, "Confirm your PIN");
        vTaskDelay(pdMS_TO_TICKS(1000));
        display_pin_create(disp, panel_handle, touch_handle);
        xSemaphoreGive(lvgl_mux);
    }
    while (1) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            bool complete = display_pin_is_complete(pin2);
            xSemaphoreGive(lvgl_mux);
            if (complete) break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "Confirmation PIN entered (length: %d)", strlen(pin2));

    // Check if PINs match
    if (strcmp(pin1, pin2) != 0) {
        ESP_LOGE(TAG, "PINs do not match!");
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            display_pin_hide();
            show_status_message(disp, "PINs do not match!\n\nRebooting...");
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    ESP_LOGI(TAG, "PINs match - generating salt and keys");

    // Show "Decrypting device..." message
    if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
        display_pin_hide();
        show_status_message(disp, "Setting Encryption");
        xSemaphoreGive(lvgl_mux);
    }

    // Generate salt
    uint8_t salt[CRYPTO_SALT_SIZE];
    esp_err_t ret = crypto_generate_salt(salt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate salt");
        return ret;
    }
    // Derive key from PIN
    ret = crypto_derive_key(pin1, salt, key_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive key");
        return ret;
    }
    // Create verification blob
    uint8_t verification[MAX_VERIFY_SIZE];
    size_t verify_len;
    ret = crypto_create_verification(key_out, verification, &verify_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create verification blob");
        return ret;
    }
    // Store salt and verification in NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_set_blob(nvs_handle, NVS_KEY_SALT, salt, CRYPTO_SALT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save salt: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    ret = nvs_set_blob(nvs_handle, NVS_KEY_VERIFY, verification, verify_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save verification: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "First boot setup complete");

    // Clear PINs from memory
    memset(pin1, 0, sizeof(pin1));
    memset(pin2, 0, sizeof(pin2));
    return ESP_OK;
} /**/


/**
 * @brief Handle normal boot PIN unlock
 */
static esp_err_t handle_normal_boot(lv_disp_t *disp, esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle, SemaphoreHandle_t lvgl_mux, uint8_t *key_out) {
    // Load salt from NVS
    ESP_LOGI(TAG, "Normal boot - loading salt and verification");
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);     // NVS init
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    uint8_t salt[CRYPTO_SALT_SIZE];                                         // Loading salt
    size_t salt_len = CRYPTO_SALT_SIZE;
    ret = nvs_get_blob(nvs_handle, NVS_KEY_SALT, salt, &salt_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load salt: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    uint8_t verification[MAX_VERIFY_SIZE];                                  // Loading verification
    size_t verify_len = MAX_VERIFY_SIZE;
    ret = nvs_get_blob(nvs_handle, NVS_KEY_VERIFY, verification, &verify_len);
    nvs_close(nvs_handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load verification: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Salt and verification loaded");
    // PIN entry loop (retry on wrong PIN)
    while (1) {
        char entered_pin[PIN_MAX_LENGTH + 1];
        // Show PIN entry screen
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            display_pin_create(disp, panel_handle, touch_handle);
            xSemaphoreGive(lvgl_mux);
        }
        // Wait for PIN entry
        while (1) {
            if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                bool complete = display_pin_is_complete(entered_pin);
                xSemaphoreGive(lvgl_mux);
                if (complete) break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "PIN entered (length: %d)", strlen(entered_pin));
        // Show "Decrypting device..." message
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            display_pin_hide();
            show_status_message(disp, "Decrypting\nDevice");
            xSemaphoreGive(lvgl_mux);
        }
        // Derive key from entered PIN
        ret = crypto_derive_key(entered_pin, salt, key_out);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to derive key");
            memset(entered_pin, 0, sizeof(entered_pin));
            return ret;
        }
        // Verify PIN
        ret = crypto_verify_pin(key_out, verification, verify_len);
        // Clear PIN from memory
        memset(entered_pin, 0, sizeof(entered_pin));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "PIN verified successfully");
            return ESP_OK;
        }
        // Wrong PIN - show error and retry
        ESP_LOGW(TAG, "Incorrect PIN");
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            show_status_message(disp, "Incorrect PIN\n\nTry again");
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
} /**/
