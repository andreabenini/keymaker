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

#include "crypto.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/gcm.h"
#include <string.h>

static const char *TAG = "crypto";


esp_err_t crypto_generate_salt(uint8_t *salt) {
    if (!salt) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_fill_random(salt, CRYPTO_SALT_SIZE);
    ESP_LOGI(TAG, "Generated %d-byte random salt", CRYPTO_SALT_SIZE);
    return ESP_OK;
} /**/


esp_err_t crypto_derive_key(const char *pin, const uint8_t *salt, uint8_t *key_out) {
    if (!pin || !salt || !key_out) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Deriving key with PBKDF2 (%d iterations)...", CRYPTO_PBKDF2_ITERATIONS);
    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(
                MBEDTLS_MD_SHA256, (const unsigned char *)pin, strlen(pin), salt,
                CRYPTO_SALT_SIZE, CRYPTO_PBKDF2_ITERATIONS, CRYPTO_KEY_SIZE, key_out);
    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 derivation failed: -0x%04x", -ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Key derivation successful");
    return ESP_OK;
} /**/


esp_err_t crypto_encrypt(const uint8_t *plaintext, size_t plaintext_len, const uint8_t *key, uint8_t *output, size_t *output_len) {
    if (!plaintext || !key || !output || !output_len) {
        return ESP_ERR_INVALID_ARG;
    }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    // Set up AES-256-GCM
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, CRYPTO_KEY_SIZE * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "GCM setkey failed: -0x%04x", -ret);
        mbedtls_gcm_free(&gcm);
        return ESP_FAIL;
    }
    // Generate random IV
    uint8_t iv[CRYPTO_IV_SIZE];
    esp_fill_random(iv, CRYPTO_IV_SIZE);
    // Output format: [IV][ciphertext][tag]
    uint8_t *ciphertext = output + CRYPTO_IV_SIZE;
    uint8_t *tag = output + CRYPTO_IV_SIZE + plaintext_len;
    // Encrypt
    ret = mbedtls_gcm_crypt_and_tag(
                &gcm,
                MBEDTLS_GCM_ENCRYPT,
                plaintext_len,
                iv, CRYPTO_IV_SIZE,
                NULL, 0,                // No additional authenticated data
                plaintext,
                ciphertext,
                CRYPTO_TAG_SIZE,
                tag);
    mbedtls_gcm_free(&gcm);
    if (ret != 0) {
        ESP_LOGE(TAG, "GCM encryption failed: -0x%04x", -ret);
        return ESP_FAIL;
    }
    // Copy IV to beginning of output
    memcpy(output, iv, CRYPTO_IV_SIZE);
    *output_len = CRYPTO_IV_SIZE + plaintext_len + CRYPTO_TAG_SIZE;
    ESP_LOGD(TAG, "Encrypted %zu bytes -> %zu bytes", plaintext_len, *output_len);
    return ESP_OK;
} /**/


esp_err_t crypto_decrypt(const uint8_t *ciphertext, size_t ciphertext_len, const uint8_t *key, uint8_t *output, size_t *output_len) {
    if (!ciphertext || !key || !output || !output_len) {
        return ESP_ERR_INVALID_ARG;
    }
    // Minimum size check: IV + tag
    if (ciphertext_len < CRYPTO_IV_SIZE + CRYPTO_TAG_SIZE) {
        ESP_LOGE(TAG, "Ciphertext too short: %zu bytes", ciphertext_len);
        return ESP_ERR_INVALID_SIZE;
    }
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    // Set up AES-256-GCM
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, CRYPTO_KEY_SIZE * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "GCM setkey failed: -0x%04x", -ret);
        mbedtls_gcm_free(&gcm);
        return ESP_FAIL;
    }
    // Parse input: [IV][ciphertext][tag]
    const uint8_t *iv = ciphertext;
    const uint8_t *encrypted_data = ciphertext + CRYPTO_IV_SIZE;
    const uint8_t *tag = ciphertext + ciphertext_len - CRYPTO_TAG_SIZE;
    size_t encrypted_len = ciphertext_len - CRYPTO_IV_SIZE - CRYPTO_TAG_SIZE;
    // Decrypt and verify tag
    ret = mbedtls_gcm_auth_decrypt(&gcm,
                    encrypted_len,
                    iv, CRYPTO_IV_SIZE,
                    NULL, 0,  // No additional authenticated data
                    tag, CRYPTO_TAG_SIZE,
                    encrypted_data,
                    output);
    mbedtls_gcm_free(&gcm);
    if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
        ESP_LOGW(TAG, "GCM authentication failed - wrong PIN or corrupted data");
        return ESP_ERR_INVALID_MAC;
    } else if (ret != 0) {
        ESP_LOGE(TAG, "GCM decryption failed: -0x%04x", -ret);
        return ESP_FAIL;
    }
    *output_len = encrypted_len;
    ESP_LOGD(TAG, "Decrypted %zu bytes -> %zu bytes", ciphertext_len, *output_len);
    return ESP_OK;
} /**/


esp_err_t crypto_create_verification(const uint8_t *key, uint8_t *verification_out, size_t *verification_len) {
    if (!key || !verification_out || !verification_len) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *verify_string = CRYPTO_VERIFICATION_STRING;
    size_t verify_len = strlen(verify_string);
    esp_err_t ret = crypto_encrypt((const uint8_t *)verify_string, verify_len, key, verification_out, verification_len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Created verification blob (%zu bytes)", *verification_len);
    }
    return ret;
} /**/


esp_err_t crypto_verify_pin(const uint8_t *key, const uint8_t *verification, size_t verification_len) {
    if (!key || !verification) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t decrypted[128];
    size_t decrypted_len;
    esp_err_t ret = crypto_decrypt(verification, verification_len, key, decrypted, &decrypted_len);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_MAC) {
            ESP_LOGW(TAG, "PIN verification failed - incorrect PIN");
        }
        return ret;
    }
    // Check if decrypted string matches
    const char *expected = CRYPTO_VERIFICATION_STRING;
    size_t expected_len = strlen(expected);
    if (decrypted_len != expected_len || memcmp(decrypted, expected, expected_len) != 0) {
        ESP_LOGE(TAG, "Verification string mismatch");
        return ESP_ERR_INVALID_MAC;
    }
    ESP_LOGI(TAG, "PIN verification successful");
    return ESP_OK;
} /**/
