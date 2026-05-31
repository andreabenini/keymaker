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

#ifndef PORTAL_H
#define PORTAL_H

#include "esp_err.h"
#include <stdbool.h>

// HTTP server buffer sizes for captive portal HTML generation
#define PORTAL_PROFILE_LIST_SIZE  12000  // Buffer for profile list HTML (supports 20 profiles)
#define PORTAL_ACTION_FORMS_SIZE  10000  // Buffer for profile action forms HTML
#define PORTAL_DYNAMIC_HTML_SIZE  32000  // Buffer for complete HTML page
#define PORTAL_HTTP_STACK_SIZE    8192   // HTTP server task stack size (8KB)

/**
 * @brief Start the captive portal
 *
 * This function:
 * - Disconnects from any existing WiFi network
 * - Starts a WiFi Access Point with SSID "keymaker-XXXX"
 * - Starts the HTTP server for the captive portal
 * - Provides DNS redirection for captive portal detection
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t portal_start(void);

/**
 * @brief Stop the captive portal
 *
 * This function:
 * - Stops the HTTP server
 * - Stops the WiFi Access Point
 * - Reconnects to the configured WiFi network (if any)
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t portal_stop(void);

/**
 * @brief Get the captive portal SSID
 *
 * @return Pointer to the SSID string (e.g., "keymaker-1234")
 */
const char* portal_get_ssid(void);

/**
 * @brief Get the captive portal URL
 *
 * @return Pointer to the URL string (e.g., "http://192.168.4.1")
 */
const char* portal_get_url(void);

/**
 * @brief Check if the captive portal is currently running
 *
 * @return true if running, false otherwise
 */
bool portal_is_running(void);

/**
 * @brief Check if the captive portal is currently stopping
 *
 * Used to prevent starting the portal while it's still shutting down.
 *
 * @return true if stopping, false otherwise
 */
bool portal_is_stopping(void);

/**
 * @brief Check if screen calibration was requested via the web interface
 *
 * @return true if calibration requested, false otherwise
 */
bool portal_calibration_requested(void);

/**
 * @brief Clear the calibration request flag
 *
 * Should be called after handling the calibration request
 */
void portal_calibration_clear(void);

/**
 * @brief Check if portal exit was requested (save or cancel)
 *
 * @return true if exit requested, false otherwise
 */
bool portal_exit_requested(void);

/**
 * @brief Clear the exit request flag
 *
 * Should be called after handling the exit request
 */
void portal_exit_clear(void);

/**
 * @brief Request portal exit
 *
 * Sets the exit flag to signal that the portal should close
 * and WiFi should be restarted
 */
void portal_request_exit(void);

#endif // PORTAL_H
