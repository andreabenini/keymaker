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

#ifndef CRYPTO_H
#define CRYPTO_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

// Cryptographic constants
#define CRYPTO_SALT_SIZE        32      // 256-bit salt
#define CRYPTO_KEY_SIZE         32      // 256-bit key for AES-256
#define CRYPTO_IV_SIZE          12      // 96-bit IV for GCM
#define CRYPTO_TAG_SIZE         16      // 128-bit authentication tag
#define CRYPTO_PBKDF2_ITERATIONS 100000 // PBKDF2 iterations

// Verification string stored encrypted to validate PIN
#define CRYPTO_VERIFICATION_STRING "KEYMAKER_VERIFIED"

/**
 * @brief Generate random salt for PBKDF2
 *
 * @param salt Buffer to store salt (must be CRYPTO_SALT_SIZE bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t crypto_generate_salt(uint8_t *salt);

/**
 * @brief Derive encryption key from PIN using PBKDF2
 *
 * Uses PBKDF2-HMAC-SHA256 with 100,000 iterations
 *
 * @param pin User's PIN string
 * @param salt Salt for key derivation (CRYPTO_SALT_SIZE bytes)
 * @param key_out Output buffer for derived key (CRYPTO_KEY_SIZE bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t crypto_derive_key(const char *pin, const uint8_t *salt, uint8_t *key_out);

/**
 * @brief Encrypt data using AES-256-GCM
 *
 * Output format: [IV (12 bytes)][ciphertext][tag (16 bytes)]
 *
 * @param plaintext Input plaintext data
 * @param plaintext_len Length of plaintext
 * @param key Encryption key (CRYPTO_KEY_SIZE bytes)
 * @param output Output buffer (must be at least plaintext_len + CRYPTO_IV_SIZE + CRYPTO_TAG_SIZE)
 * @param output_len Pointer to store actual output length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t crypto_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                         const uint8_t *key, uint8_t *output, size_t *output_len);

/**
 * @brief Decrypt data using AES-256-GCM
 *
 * Input format: [IV (12 bytes)][ciphertext][tag (16 bytes)]
 *
 * @param ciphertext Input ciphertext (includes IV and tag)
 * @param ciphertext_len Total length of ciphertext + IV + tag
 * @param key Decryption key (CRYPTO_KEY_SIZE bytes)
 * @param output Output buffer for plaintext
 * @param output_len Pointer to store actual plaintext length
 * @return ESP_OK on success, ESP_ERR_INVALID_MAC if authentication fails, error otherwise
 */
esp_err_t crypto_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                         const uint8_t *key, uint8_t *output, size_t *output_len);

/**
 * @brief Create verification blob to validate PIN correctness
 *
 * Encrypts a known string with the derived key. Used to check if PIN is correct
 * without storing the PIN or key.
 *
 * @param key Encryption key derived from PIN
 * @param verification_out Output buffer for verification blob
 * @param verification_len Pointer to store verification blob length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t crypto_create_verification(const uint8_t *key, uint8_t *verification_out, size_t *verification_len);

/**
 * @brief Verify PIN by attempting to decrypt verification blob
 *
 * @param key Encryption key derived from entered PIN
 * @param verification Verification blob from NVS
 * @param verification_len Length of verification blob
 * @return ESP_OK if PIN is correct, ESP_ERR_INVALID_MAC if wrong PIN, error otherwise
 */
esp_err_t crypto_verify_pin(const uint8_t *key, const uint8_t *verification, size_t verification_len);

#endif // CRYPTO_H
