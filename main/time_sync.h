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
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"


/**
 * @brief Initialize and start NTP time synchronization
 *
 * Should be called when WiFi connection is established.
 * Uses pool.ntp.org by default.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t time_sync_start(void);


/**
 * @brief Stop NTP time synchronization
 *
 * Should be called when WiFi is disconnected.
 */
void time_sync_stop(void);


/**
 * @brief Check if time has been synchronized
 *
 * @return true if time is synchronized, false otherwise
 */
bool time_sync_is_synchronized(void);


/**
 * @brief Get current Unix timestamp
 *
 * @return Unix timestamp in seconds, or 0 if time is not synchronized
 */
uint64_t time_sync_get_unix_time(void);


#endif // TIME_SYNC_H
