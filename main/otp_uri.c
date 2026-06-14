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

#include "otp_uri.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "otp_uri";


/**
 * Check if string starts with prefix
 */
static bool starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
} /**/


/**
 * URL decode a string
 */
static void url_decode(char *dst, const char *src, size_t max_len) {
    char a, b;
    size_t written = 0;
    while (*src && written < max_len - 1) {
        if (*src == '%' && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            dst[written++] = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            dst[written++] = ' ';
            src++;
        } else {
            dst[written++] = *src++;
        }
    }
    dst[written] = '\0';
} /**/


/**
 * Helper function to extract query parameter value
 */
static bool get_query_param(const char *query, const char *key, char *value, size_t max_len) {
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    const char *start = strstr(query, search_key);
    if (!start) {
        return false;
    }
    start += strlen(search_key);
    const char *end = strchr(start, '&');
    size_t len = end ? (end - start) : strlen(start);
    if (len >= max_len) {
        len = max_len - 1;
    }
    char temp[256];
    strncpy(temp, start, len);
    temp[len] = '\0';
    url_decode(value, temp, max_len);
    return true;
} /**/


/**
 * Detect if str is a valid base32 string
 */
bool is_valid_base32(const char *str) {
    if (!str || strlen(str) == 0) {
        return false;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        // Base32 alphabet: A-Z, 2-7, optional padding =
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= '2' && c <= '7') ||
              c == '=')) {
            return false;
        }
    }
    return true;
} /**/


/**
 * Parse OTP Authentication URI
 */
bool parse_otpauth_uri(const char *uri, otp_profile_t *profile) {
    if (!uri || !profile) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    // Initialize profile with defaults
    memset(profile, 0, sizeof(otp_profile_t));
    profile->digits = 6;
    profile->period = 30;
    profile->counter = 0;
    // Check for otpauth:// prefix
    if (!starts_with(uri, "otpauth://")) {
        ESP_LOGE(TAG, "URI must start with otpauth://");
        return false;
    }
    const char *ptr = uri + 10; // Skip "otpauth://"
    // Extract type (totp or hotp)
    if (starts_with(ptr, "totp/")) {
        profile->type = OTP_TYPE_TOTP;
        ptr += 5;
    } else if (starts_with(ptr, "hotp/")) {
        profile->type = OTP_TYPE_HOTP;
        ptr += 5;
    } else {
        ESP_LOGE(TAG, "Invalid OTP type, must be 'totp' or 'hotp'");
        return false;
    }
    // Extract label (everything before '?')
    const char *query_start = strchr(ptr, '?');
    if (!query_start) {
        ESP_LOGE(TAG, "No query parameters found in URI");
        return false;
    }
    size_t label_len = query_start - ptr;
    if (label_len == 0 || label_len >= CONFIG_MAX_PROFILE_NAME) {
        ESP_LOGE(TAG, "Label length invalid: %d", label_len);
        return false;
    }
    char temp_label[CONFIG_MAX_PROFILE_NAME * 3];
    strncpy(temp_label, ptr, label_len);
    temp_label[label_len] = '\0';
    url_decode(profile->label, temp_label, CONFIG_MAX_PROFILE_NAME);
    ESP_LOGI(TAG, "Parsed label: '%s'", profile->label);
    // Move to query parameters
    const char *query = query_start + 1;
    // Extract secret (required)
    char secret[CONFIG_MAX_SECRET_LEN];
    if (!get_query_param(query, "secret", secret, sizeof(secret))) {
        ESP_LOGE(TAG, "Secret parameter is required");
        return false;
    }
    // Convert secret to uppercase (Base32 is case-insensitive)
    for (size_t i = 0; secret[i]; i++) {
        secret[i] = toupper(secret[i]);
    }
    if (!is_valid_base32(secret)) {
        ESP_LOGE(TAG, "Invalid Base32 secret");
        return false;
    }
    strncpy(profile->secret, secret, CONFIG_MAX_SECRET_LEN - 1);
    ESP_LOGI(TAG, "Parsed secret: %d chars", strlen(profile->secret));
    // Extract issuer (optional)
    char issuer[CONFIG_MAX_PROFILE_NAME];
    if (get_query_param(query, "issuer", issuer, sizeof(issuer))) {
        strncpy(profile->issuer, issuer, CONFIG_MAX_PROFILE_NAME - 1);
        ESP_LOGI(TAG, "Parsed issuer: '%s'", profile->issuer);
    } else {
        // Try to extract issuer from label (format: "Issuer:Account")
        char *colon = strchr(profile->label, ':');
        if (colon && colon > profile->label) {
            size_t issuer_len = colon - profile->label;
            if (issuer_len < CONFIG_MAX_PROFILE_NAME) {
                strncpy(profile->issuer, profile->label, issuer_len);
                profile->issuer[issuer_len] = '\0';
                ESP_LOGI(TAG, "Extracted issuer from label: '%s'", profile->issuer);
            }
        }
    }
    // Extract digits (optional)
    char digits_str[8];
    if (get_query_param(query, "digits", digits_str, sizeof(digits_str))) {
        int digits = atoi(digits_str);
        if (digits == 6 || digits == 8) {
            profile->digits = digits;
            ESP_LOGI(TAG, "Parsed digits: %d", profile->digits);
        } else {
            ESP_LOGW(TAG, "Invalid digits value: %d, using default 6", digits);
        }
    }
    // Extract period (TOTP only)
    if (profile->type == OTP_TYPE_TOTP) {
        char period_str[16];
        if (get_query_param(query, "period", period_str, sizeof(period_str))) {
            int period = atoi(period_str);
            if (period > 0 && period <= 300) {
                profile->period = period;
                ESP_LOGI(TAG, "Parsed period: %d", profile->period);
            } else {
                ESP_LOGW(TAG, "Invalid period value: %d, using default 30", period);
            }
        }
    }
    // Extract counter (HOTP only)
    if (profile->type == OTP_TYPE_HOTP) {
        char counter_str[32];
        if (get_query_param(query, "counter", counter_str, sizeof(counter_str))) {
            profile->counter = strtoull(counter_str, NULL, 10);
            ESP_LOGI(TAG, "Parsed counter: %llu", profile->counter);
        }
    }
    // Generate default icon from label
    config_generate_default_icon(profile);
    ESP_LOGI(TAG, "URI parsed successfully: type=%s, label='%s', issuer='%s', digits=%d",
             profile->type == OTP_TYPE_TOTP ? "TOTP" : "HOTP",
             profile->label, profile->issuer, profile->digits);
    return true;
} /**/
