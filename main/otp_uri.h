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

#ifndef OTP_URI_H
#define OTP_URI_H

#include "config.h"
#include <stdbool.h>

/**
 * @brief Parse an otpauth:// URI into an OTP profile structure
 *
 * Parses URIs in the format:
 * otpauth://totp/Label?secret=BASE32&issuer=Issuer&digits=6&period=30
 * otpauth://hotp/Label?secret=BASE32&counter=0
 *
 * @param uri The otpauth:// URI string
 * @param profile Pointer to profile structure to populate
 * @return true if URI was successfully parsed, false otherwise
 */
bool parse_otpauth_uri(const char *uri, otp_profile_t *profile);

/**
 * @brief Validate a Base32 encoded string
 *
 * Base32 alphabet: A-Z, 2-7, and optional padding '='
 *
 * @param str String to validate
 * @return true if valid Base32, false otherwise
 */
bool is_valid_base32(const char *str);

#endif // OTP_URI_H
