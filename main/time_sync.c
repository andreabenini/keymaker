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
#include "time_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <time.h>
#include <sys/time.h>


static const char *TAG = "time_sync";
static bool time_synchronized = false;


/**
 * @brief Callback function called when time is synchronized
 */
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synchronized! Current time: %lld", (long long)tv->tv_sec);
    time_synchronized = true;
} /**/


esp_err_t time_sync_start(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    // Set timezone to UTC
    setenv("TZ", "UTC", 1);
    tzset();
    // Initialize SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP initialized, time sync will happen asynchronously");
    ESP_LOGI(TAG, "Callback will be triggered when time is synchronized");
    return ESP_OK;
} /**/


void time_sync_stop(void) {
    ESP_LOGI(TAG, "Stopping SNTP");
    esp_sntp_stop();
    time_synchronized = false;
} /**/


bool time_sync_is_synchronized(void) {
    // Just check the flag.
    // SNTP status can change after initial sync, the callback sets this flag when sync completes and clear it when stopping
    if (!time_synchronized) {
        ESP_LOGD(TAG, "time_sync_is_synchronized: false (flag not set)");
        return false;
    }
    // Also verify we can get a valid time
    time_t now;
    time(&now);
    bool valid = (now > 1000000000);  // After year 2001 (sanity check)
    if (!valid) {
        ESP_LOGW(TAG, "time_sync_is_synchronized: false (invalid time: %lld)", (long long)now);
    }
    return valid;
} /**/


uint64_t time_sync_get_unix_time(void) {
    if (!time_sync_is_synchronized()) {
        ESP_LOGW(TAG, "Time not synchronized yet");
        return 0;
    }
    time_t now;
    time(&now);
    return (uint64_t)now;
} /**/
