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

#ifndef TOTP_H
#define TOTP_H

#include "config.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Generate a TOTP code for the given profile
 *
 * @param profile Pointer to the OTP profile containing secret, period, and digits
 * @param unix_time Current Unix timestamp (seconds since epoch)
 * @param code_out Buffer to store the generated code (minimum 9 bytes for 8-digit + null)
 * @return true if code was generated successfully, false on error
 */
bool totp_generate(const otp_profile_t *profile, uint64_t unix_time, char *code_out);

/**
 * @brief Get the number of seconds remaining in the current TOTP period
 *
 * @param unix_time Current Unix timestamp
 * @param period TOTP period in seconds (typically 30)
 * @return Seconds remaining until next code (0 to period-1)
 */
uint32_t totp_get_remaining_seconds(uint64_t unix_time, uint32_t period);

#endif // TOTP_H
