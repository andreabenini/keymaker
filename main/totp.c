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
#include "totp.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "totp";


static int base32_char_to_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    if (c >= '2' && c <= '7') {
        return 26 + (c - '2');
    }
    if (c == '=') {
        return -2;      // Padding
    }
    return -1;          // Invalid
} /**/


static int base32_decode(const char *base32, uint8_t *output, size_t output_len) {
    if (!base32 || !output) {
        return -1;
    }
    size_t input_len = strlen(base32);
    size_t output_pos = 0;
    uint32_t buffer = 0;
    int bits_in_buffer = 0;
    for (size_t i = 0; i < input_len; i++) {
        int value = base32_char_to_value(base32[i]);
        if (value == -2) {
            // Padding character, stop decoding
            break;
        }
        if (value == -1) {
            ESP_LOGE(TAG, "Invalid Base32 character: %c", base32[i]);
            return -1;
        }
        // Add 5 bits to buffer
        buffer = (buffer << 5) | value;
        bits_in_buffer += 5;
        // Extract complete bytes (8 bits)
        while (bits_in_buffer >= 8) {
            if (output_pos >= output_len) {
                ESP_LOGE(TAG, "Output buffer too small");
                return -1;
            }
            bits_in_buffer -= 8;
            output[output_pos++] = (buffer >> bits_in_buffer) & 0xFF;
        }
    }
    return output_pos;
} /**/


static bool hmac_sha1(const uint8_t *key, size_t key_len, const uint8_t *message, size_t message_len, uint8_t *output) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info) {
        ESP_LOGE(TAG, "Failed to get SHA1 info");
        return false;
    }
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {
        ESP_LOGE(TAG, "Failed to setup HMAC context");
        mbedtls_md_free(&ctx);
        return false;
    }
    if (mbedtls_md_hmac_starts(&ctx, key, key_len) != 0) {
        ESP_LOGE(TAG, "Failed to start HMAC");
        mbedtls_md_free(&ctx);
        return false;
    }
    if (mbedtls_md_hmac_update(&ctx, message, message_len) != 0) {
        ESP_LOGE(TAG, "Failed to update HMAC");
        mbedtls_md_free(&ctx);
        return false;
    }
    if (mbedtls_md_hmac_finish(&ctx, output) != 0) {
        ESP_LOGE(TAG, "Failed to finish HMAC");
        mbedtls_md_free(&ctx);
        return false;
    }
    mbedtls_md_free(&ctx);
    return true;
} /**/


static bool totp_generate_code(const uint8_t *secret, size_t secret_len, uint64_t time_counter, uint8_t digits, char *code_out) {
    // Convert time counter to 8-byte big-endian
    uint8_t message[8];
    for (int i = 7; i >= 0; i--) {
        message[i] = time_counter & 0xFF;
        time_counter >>= 8;
    }
    // Generate HMAC-SHA1
    uint8_t hash[20];
    if (!hmac_sha1(secret, secret_len, message, sizeof(message), hash)) {
        return false;
    }
    // Dynamic truncation (RFC 4226)
    int offset = hash[19] & 0x0F;
    uint32_t binary =
                    ((hash[offset] & 0x7F) << 24) |
                    ((hash[offset + 1] & 0xFF) << 16) |
                    ((hash[offset + 2] & 0xFF) << 8) |
                    (hash[offset + 3] & 0xFF);
    // Calculate modulo to get final code
    uint32_t divisor = 1;
    for (int i = 0; i < digits; i++) {
        divisor *= 10;
    }
    uint32_t code = binary % divisor;
    // Format with leading zeros
    snprintf(code_out, digits + 1, "%0*lu", digits, (unsigned long)code);
    return true;
} /**/


bool totp_generate(const otp_profile_t *profile, uint64_t unix_time, char *code_out) {
    if (!profile || !code_out) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    if (profile->type != OTP_TYPE_TOTP) {
        ESP_LOGE(TAG, "Profile is not TOTP type");
        return false;
    }
    // Decode Base32 secret
    uint8_t secret_bytes[CONFIG_MAX_SECRET_LEN];
    int secret_len = base32_decode(profile->secret, secret_bytes, sizeof(secret_bytes));
    if (secret_len < 0) {
        ESP_LOGE(TAG, "Failed to decode Base32 secret");
        return false;
    }
    ESP_LOGI(TAG, "Decoded secret: %d bytes", secret_len);
    // Calculate time counter
    uint64_t time_counter = unix_time / profile->period;
    ESP_LOGI(TAG, "Time counter: %llu (unix_time=%llu, period=%u)", time_counter, unix_time, profile->period);
    // Generate TOTP code
    if (!totp_generate_code(secret_bytes, secret_len, time_counter, profile->digits, code_out)) {
        ESP_LOGE(TAG, "Failed to generate TOTP code");
        return false;
    }
    ESP_LOGI(TAG, "Generated TOTP code: %s", code_out);
    return true;
} /**/


uint32_t totp_get_remaining_seconds(uint64_t unix_time, uint32_t period) {
    return period - (unix_time % period);
} /**/
