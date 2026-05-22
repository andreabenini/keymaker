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

#include "config.h"
#include "crypto.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "config";

#define NVS_NAMESPACE "keymaker"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"
#define NVS_KEY_PROFILE_COUNT "prof_count"
#define NVS_KEY_PROFILE_PREFIX "prof_"

// Global config cache
static keymaker_config_t g_config;
static bool g_config_loaded = false;

// Global encryption key
static uint8_t g_encryption_key[CRYPTO_KEY_SIZE];
static bool g_encryption_key_set = false;


/**
 * @brief Configuration init method/function
 */
esp_err_t config_init() {
    ESP_LOGI(TAG, "Initializing configuration system");
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize config structure with defaults
    memset(&g_config, 0, sizeof(keymaker_config_t));
    g_config_loaded = false;

    ESP_LOGI(TAG, "Configuration system initialized");
    return ESP_OK;
} /**/


/**
 * @brief Encrypt and store a string in NVS
 */
static esp_err_t nvs_set_encrypted_str(nvs_handle_t handle, const char *key, const char *value);


/**
 * @brief Read and decrypt a string from NVS
 */
static esp_err_t nvs_get_encrypted_str(nvs_handle_t handle, const char *key, char *out_value, size_t max_len);


/**
 * @brief Serialize profile to JSON string
 */
static char* profile_to_json(const otp_profile_t *profile) {
    if (!profile) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "label", profile->label);
    cJSON_AddStringToObject(root, "issuer", profile->issuer);
    cJSON_AddStringToObject(root, "icon", profile->icon);
    cJSON_AddStringToObject(root, "secret", profile->secret);
    cJSON_AddNumberToObject(root, "type", profile->type);
    cJSON_AddNumberToObject(root, "digits", profile->digits);
    cJSON_AddNumberToObject(root, "period", profile->period);
    cJSON_AddNumberToObject(root, "counter", profile->counter);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
} /**/


/**
 * @brief Deserialize profile from JSON string
 */
static esp_err_t profile_from_json(const char *json_str, otp_profile_t *profile) {
    if (!json_str || !profile) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse profile JSON");
        return ESP_FAIL;
    }
    cJSON *item;
    // Label
    item = cJSON_GetObjectItem(root, "label");
    if (item && cJSON_IsString(item)) {
        strncpy(profile->label, item->valuestring, CONFIG_MAX_PROFILE_NAME - 1);
        profile->label[CONFIG_MAX_PROFILE_NAME - 1] = '\0';
    }
    // Issuer
    item = cJSON_GetObjectItem(root, "issuer");
    if (item && cJSON_IsString(item)) {
        strncpy(profile->issuer, item->valuestring, CONFIG_MAX_PROFILE_NAME - 1);
        profile->issuer[CONFIG_MAX_PROFILE_NAME - 1] = '\0';
    }
    // Icon
    item = cJSON_GetObjectItem(root, "icon");
    if (item && cJSON_IsString(item)) {
        strncpy(profile->icon, item->valuestring, 2);
        profile->icon[2] = '\0';
    }
    // Secret
    item = cJSON_GetObjectItem(root, "secret");
    if (item && cJSON_IsString(item)) {
        strncpy(profile->secret, item->valuestring, CONFIG_MAX_SECRET_LEN - 1);
        profile->secret[CONFIG_MAX_SECRET_LEN - 1] = '\0';
    }
    // Type
    item = cJSON_GetObjectItem(root, "type");
    if (item && cJSON_IsNumber(item)) {
        profile->type = (otp_type_t)item->valueint;
    } else {
        profile->type = OTP_TYPE_TOTP;
    }
    // Digits
    item = cJSON_GetObjectItem(root, "digits");
    if (item && cJSON_IsNumber(item)) {
        profile->digits = item->valueint;
    } else {
        profile->digits = 6;
    }
    // Period
    item = cJSON_GetObjectItem(root, "period");
    if (item && cJSON_IsNumber(item)) {
        profile->period = item->valueint;
    } else {
        profile->period = 30;
    }
    // Counter
    item = cJSON_GetObjectItem(root, "counter");
    if (item && cJSON_IsNumber(item)) {
        profile->counter = item->valuedouble; // Use valuedouble for 64-bit
    } else {
        profile->counter = 0;
    }
    cJSON_Delete(root);
    return ESP_OK;
} /**/


esp_err_t config_load(keymaker_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Loading configuration from NVS");
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No configuration found in NVS");
        return ESP_ERR_NOT_FOUND;
    }
    // Load WiFi SSID (encrypted)
    ret = nvs_get_encrypted_str(nvs_handle, NVS_KEY_WIFI_SSID, config->wifi_ssid, CONFIG_MAX_SSID_LEN);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_MAC) {
            ESP_LOGE(TAG, "WiFi SSID corrupted - decryption failed");
        } else {
            ESP_LOGW(TAG, "WiFi SSID not found");
        }
        config->wifi_ssid[0] = '\0';
    }
    // Load WiFi password (encrypted)
    ret = nvs_get_encrypted_str(nvs_handle, NVS_KEY_WIFI_PASS, config->wifi_password, CONFIG_MAX_PASSWORD_LEN);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_MAC) {
            ESP_LOGE(TAG, "WiFi password corrupted - decryption failed");
        } else {
            ESP_LOGW(TAG, "WiFi password not found");
        }
        config->wifi_password[0] = '\0';
    }
    // Load profile count
    uint8_t count = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_PROFILE_COUNT, &count);
    if (ret != ESP_OK || count > CONFIG_MAX_PROFILES) {
        ESP_LOGW(TAG, "Profile count not found or invalid");
        count = 0;
    }
    config->profile_count = count;
    // Load profiles (encrypted JSON format)
    uint8_t valid_profiles = 0;
    for (uint8_t i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "profile_%d_enc", i);
        // Allocate buffer for encrypted JSON
        char json_buf[1024];
        ret = nvs_get_encrypted_str(nvs_handle, key, json_buf, sizeof(json_buf));
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_MAC) {
                ESP_LOGE(TAG, "Profile %d corrupted - decryption failed", i);
            } else {
                ESP_LOGW(TAG, "Profile %d not found", i);
            }
            // Skip this profile but continue loading others
            continue;
        }
        // Deserialize JSON to profile
        ret = profile_from_json(json_buf, &config->profiles[valid_profiles]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Profile %d JSON parsing failed", i);
            continue;
        }
        ESP_LOGI(TAG, "Loaded profile %d: %s", i, config->profiles[valid_profiles].label);
        valid_profiles++;
    }
    // Loading profiles count
    config->profile_count = valid_profiles;
    if (valid_profiles < count) {
        ESP_LOGW(TAG, "WARNING: Loaded %d/%d profiles - %d profile(s) corrupted or invalid",
                 valid_profiles, count, count - valid_profiles);
        ESP_LOGW(TAG, "Corrupted profiles have been removed. Use configuration menu to re-add them.");
    }
    ESP_LOGI(TAG, "Successfully loaded %d profiles", valid_profiles);
    nvs_close(nvs_handle);
    // Cache the loaded config
    memcpy(&g_config, config, sizeof(keymaker_config_t));
    g_config_loaded = true;
    ESP_LOGI(TAG, "Configuration loaded: SSID='%s', profiles=%d",
             config->wifi_ssid, config->profile_count);
    return ESP_OK;
} /**/


/**
 * @brief Save configuration to NVS storage
 */
esp_err_t config_save(const keymaker_config_t *config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Saving configuration to NVS");
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    // Save WiFi SSID (encrypted)
    ret = nvs_set_encrypted_str(nvs_handle, NVS_KEY_WIFI_SSID, config->wifi_ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    // Save WiFi password (encrypted)
    ret = nvs_set_encrypted_str(nvs_handle, NVS_KEY_WIFI_PASS, config->wifi_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    // Save profile count
    ret = nvs_set_u8(nvs_handle, NVS_KEY_PROFILE_COUNT, config->profile_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save profile count: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    // Save profiles (encrypted JSON format)
    for (uint8_t i = 0; i < config->profile_count; i++) {
        // Serialize profile to JSON
        char *json_str = profile_to_json(&config->profiles[i]);
        if (!json_str) {
            ESP_LOGE(TAG, "Failed to serialize profile %d to JSON", i);
            nvs_close(nvs_handle);
            return ESP_ERR_NO_MEM;
        }
        // Save encrypted JSON
        char key[32];
        snprintf(key, sizeof(key), "profile_%d_enc", i);
        ret = nvs_set_encrypted_str(nvs_handle, key, json_str);
        free(json_str); // Free JSON string allocated by cJSON
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save profile %d: %s", i, esp_err_to_name(ret));
            nvs_close(nvs_handle);
            return ret;
        }
        ESP_LOGI(TAG, "Saved profile %d: %s", i, config->profiles[i].label);
    }
    // Commit changes
    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    nvs_close(nvs_handle);
    // Update cache
    memcpy(&g_config, config, sizeof(keymaker_config_t));
    g_config_loaded = true;
    ESP_LOGI(TAG, "Configuration saved: SSID='%s', profiles=%d",
             config->wifi_ssid, config->profile_count);
    return ESP_OK;
} /**/


/**
 * @brief Clearing/wiping active configuration
 */
esp_err_t config_clear() {
    ESP_LOGI(TAG, "Clearing configuration");
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = nvs_erase_all(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    // Clear cache
    memset(&g_config, 0, sizeof(keymaker_config_t));
    g_config_loaded = false;
    ESP_LOGI(TAG, "Configuration cleared");
    return ret;
} /**/


/**
 * @brief Loading active wifi SSID
 */
esp_err_t config_get_wifi_ssid(char *ssid) {
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_config_loaded) {
        esp_err_t ret = config_load(&g_config);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    strncpy(ssid, g_config.wifi_ssid, CONFIG_MAX_SSID_LEN);
    return ESP_OK;
} /**/


/**
 * @brief Loading WiFi password
 */
esp_err_t config_get_wifi_password(char *password) {
    if (!password) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_config_loaded) {
        esp_err_t ret = config_load(&g_config);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    strncpy(password, g_config.wifi_password, CONFIG_MAX_PASSWORD_LEN);
    return ESP_OK;
} /**/


/**
 * @brief Generate default fancy icon for a profile
 */
void config_generate_default_icon(otp_profile_t *profile) {
    if (!profile) {
        return;
    }
    // Extract first 2 alphanumeric characters from label
    int icon_len = 0;
    for (int i = 0; i < strlen(profile->label) && icon_len < 2; i++) {
        if (isalnum((unsigned char)profile->label[i])) {
            profile->icon[icon_len++] = toupper((unsigned char)profile->label[i]);
        }
    }
    // Null terminate
    profile->icon[icon_len] = '\0';
    // Fallback if no alphanumeric chars found
    if (icon_len == 0) {
        strcpy(profile->icon, "??");
    }
    ESP_LOGI(TAG, "Generated icon '%s' for profile '%s'", profile->icon, profile->label);
} /**/


/**
 * @brief Setting encryption key on [g_encryption_key]
 */
void config_set_encryption_key(const uint8_t *key) {
    if (!key) {
        ESP_LOGE(TAG, "Invalid encryption key");
        return;
    }
    memcpy(g_encryption_key, key, CRYPTO_KEY_SIZE);
    g_encryption_key_set = true;
    ESP_LOGI(TAG, "Encryption key set");
} /**/


/**
 * @brief Encrypt and store a string in NVS
 */
static esp_err_t nvs_set_encrypted_str(nvs_handle_t handle, const char *key, const char *value) {
    if (!g_encryption_key_set) {
        ESP_LOGE(TAG, "Encryption key not set");
        return ESP_ERR_INVALID_STATE;
    }
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t value_len = strlen(value);
    if (value_len == 0) {
        // Store empty encrypted blob for empty strings
        uint8_t empty_encrypted[CRYPTO_IV_SIZE + CRYPTO_TAG_SIZE];
        size_t encrypted_len;
        esp_err_t ret = crypto_encrypt((const uint8_t *)"", 0, g_encryption_key, empty_encrypted, &encrypted_len);
        if (ret != ESP_OK) {
            return ret;
        }
        return nvs_set_blob(handle, key, empty_encrypted, encrypted_len);
    }
    // Allocate buffer for encrypted data: IV + ciphertext + tag
    size_t max_encrypted_len = value_len + CRYPTO_IV_SIZE + CRYPTO_TAG_SIZE;
    uint8_t *encrypted = malloc(max_encrypted_len);
    if (!encrypted) {
        ESP_LOGE(TAG, "Failed to allocate encryption buffer");
        return ESP_ERR_NO_MEM;
    }
    size_t encrypted_len;
    esp_err_t ret = crypto_encrypt((const uint8_t *)value, value_len, g_encryption_key, encrypted, &encrypted_len);
    if (ret != ESP_OK) {
        free(encrypted);
        return ret;
    }
    ret = nvs_set_blob(handle, key, encrypted, encrypted_len);
    free(encrypted);
    return ret;
} /**/


/**
 * @brief Read and decrypt a string from NVS
 */
static esp_err_t nvs_get_encrypted_str(nvs_handle_t handle, const char *key, char *out_value, size_t max_len) {
    if (!g_encryption_key_set) {
        ESP_LOGE(TAG, "Encryption key not set");
        return ESP_ERR_INVALID_STATE;
    }
    if (!out_value) {
        return ESP_ERR_INVALID_ARG;
    }
    // Get encrypted blob size
    size_t encrypted_len = 0;
    esp_err_t ret = nvs_get_blob(handle, key, NULL, &encrypted_len);
    if (ret != ESP_OK) {
        return ret;
    }
    // Allocate buffer for encrypted data
    uint8_t *encrypted = malloc(encrypted_len);
    if (!encrypted) {
        ESP_LOGE(TAG, "Failed to allocate decryption buffer");
        return ESP_ERR_NO_MEM;
    }
    // Read encrypted blob
    ret = nvs_get_blob(handle, key, encrypted, &encrypted_len);
    if (ret != ESP_OK) {
        free(encrypted);
        return ret;
    }
    // Decrypt
    uint8_t *decrypted = malloc(max_len);
    if (!decrypted) {
        ESP_LOGE(TAG, "Failed to allocate plaintext buffer");
        free(encrypted);
        return ESP_ERR_NO_MEM;
    }
    size_t decrypted_len;
    ret = crypto_decrypt(encrypted, encrypted_len, g_encryption_key, decrypted, &decrypted_len);
    free(encrypted);
    if (ret != ESP_OK) {
        free(decrypted);
        if (ret == ESP_ERR_INVALID_MAC) {
            ESP_LOGE(TAG, "Decryption failed for key '%s' - corrupted or wrong PIN", key);
        }
        return ret;
    }
    // Copy to output and null-terminate
    if (decrypted_len >= max_len) {
        ESP_LOGE(TAG, "Decrypted string too long: %zu >= %zu", decrypted_len, max_len);
        free(decrypted);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_value, decrypted, decrypted_len);
    out_value[decrypted_len] = '\0';
    free(decrypted);
    return ESP_OK;
} /**/
