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

#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "crypto.h"
#include <stdbool.h>
#include <stdint.h>

// Maximum sizes for configuration fields
#define CONFIG_MAX_SSID_LEN       32
#define CONFIG_MAX_PASSWORD_LEN   64
#define CONFIG_MAX_PROFILES       20
#define CONFIG_MAX_PROFILE_NAME   32
#define CONFIG_MAX_SECRET_LEN     64


/**
 * @brief OTP profile type
 */
typedef enum {
    OTP_TYPE_TOTP = 0,  // Time-based OTP
    OTP_TYPE_HOTP = 1,  // HMAC-based OTP
} otp_type_t;


/**
 * @brief OTP profile configuration
 */
typedef struct {
    char label[CONFIG_MAX_PROFILE_NAME];     // Profile label (e.g., "Google:user@gmail.com")
    char issuer[CONFIG_MAX_PROFILE_NAME];    // Profile issuer (e.g., "Google")
    char icon[3];                            // Icon (2 ASCII chars + null terminator)
    otp_type_t type;                         // TOTP or HOTP
    char secret[CONFIG_MAX_SECRET_LEN];      // Base32-encoded secret
    uint32_t period;                         // TOTP period in seconds (default: 30)
    uint64_t counter;                        // HOTP counter
    uint8_t digits;                          // Number of digits (6 or 8)
} otp_profile_t;


/**
 * @brief Keymaker configuration structure
 */
typedef struct {
    // WiFi settings
    char wifi_ssid[CONFIG_MAX_SSID_LEN];
    char wifi_password[CONFIG_MAX_PASSWORD_LEN];

    // OTP profiles
    otp_profile_t profiles[CONFIG_MAX_PROFILES];
    uint8_t profile_count;
} keymaker_config_t;


/**
 * @brief Initialize the configuration system
 *
 * Initializes NVS and loads configuration from flash
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_init(void);


/**
 * @brief Load configuration from NVS
 *
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no config exists, error code otherwise
 */
esp_err_t config_load(keymaker_config_t *config);


/**
 * @brief Save configuration to NVS
 *
 * @param config Pointer to configuration structure to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_save(const keymaker_config_t *config);


/**
 * @brief Clear all configuration from NVS
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_clear(void);


/**
 * @brief Get current WiFi SSID from config
 *
 * @param ssid Buffer to store SSID (min CONFIG_MAX_SSID_LEN bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_wifi_ssid(char *ssid);


/**
 * @brief Get current WiFi password from config
 *
 * @param password Buffer to store password (min CONFIG_MAX_PASSWORD_LEN bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_wifi_password(char *password);


/**
 * @brief Generate default icon text from profile label
 *
 * Extracts first 2 alphanumeric characters from label and converts to uppercase.
 * If less than 2 alphanumeric chars found, uses "??" as fallback.
 *
 * @param profile Pointer to profile to update (icon field will be set)
 */
void config_generate_default_icon(otp_profile_t *profile);


/**
 * @brief Set the encryption key for config operations
 *
 * This key is used to encrypt/decrypt all sensitive data in NVS.
 * Must be called after PIN unlock before loading/saving config.
 *
 * @param key Encryption key (CRYPTO_KEY_SIZE bytes)
 */
void config_set_encryption_key(const uint8_t *key);


#endif // CONFIG_H
