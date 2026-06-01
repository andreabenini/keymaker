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
#include "portal.h"
#include "config.h"
#include "touch_calibration.h"
#include "otp_uri.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "portal";

// Portal state
static bool g_portal_running = false;
static bool g_portal_stopping = false;  // Flag to prevent start while stopping
static bool g_calibration_requested = false;  // Flag for calibration request
static bool g_exit_requested = false;  // Flag for exit request (save or cancel)
static httpd_handle_t g_server = NULL;
static esp_netif_t *g_ap_netif = NULL;  // Store netif handle for cleanup
static char g_ssid[32] = {0};
static char g_url[32] = "http://192.168.4.1";

// Working configuration (in-memory during portal session)
static keymaker_config_t g_working_config;
static bool g_config_initialized = false;

// DNS server state
static TaskHandle_t g_dns_task = NULL;
static int g_dns_socket = -1;

// Default AP configuration
#define AP_IP_ADDR "192.168.4.1"
#define AP_GATEWAY "192.168.4.1"
#define AP_NETMASK "255.255.255.0"

// DNS server configuration
#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512

// DNS packet header structure
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answers;
    uint16_t authority;
    uint16_t additional;
} __attribute__((packed)) dns_header_t;


/**
 * URL decode helper function
 */
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%' && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a+b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
} /**/


/**
 * Parse URL-encoded form data
 */
static bool parse_form_data(const char *data, char *ssid, char *password) {
    ESP_LOGI(TAG, "Parsing form data: '%s'", data);
    const char *ssid_start = strstr(data, "ssid=");
    const char *pass_start = strstr(data, "password=");
    if (!ssid_start) {
        ESP_LOGE(TAG, "SSID field not found in form data");
        return false;
    }
    ssid_start += 5; // Skip "ssid="

    // Find end of ssid value (& or end of string)
    char *ssid_end = strchr(ssid_start, '&');
    size_t ssid_len = ssid_end ? (ssid_end - ssid_start) : strlen(ssid_start);
    if (ssid_len == 0 || ssid_len >= CONFIG_MAX_SSID_LEN) {
        ESP_LOGE(TAG, "SSID length invalid: %d", ssid_len);
        return false;
    }
    // Extract and decode SSID
    char temp_ssid[CONFIG_MAX_SSID_LEN * 3]; // Allow for URL encoding
    strncpy(temp_ssid, ssid_start, ssid_len);
    temp_ssid[ssid_len] = '\0';
    url_decode(ssid, temp_ssid);
    // Extract password (optional)
    if (pass_start) {
        pass_start += 9; // Skip "password="
        char *pass_end = strchr(pass_start, '&');
        size_t pass_len = pass_end ? (pass_end - pass_start) : strlen(pass_start);
        if (pass_len >= CONFIG_MAX_PASSWORD_LEN) {
            ESP_LOGE(TAG, "Password length too long: %d", pass_len);
            return false;
        }
        if (pass_len > 0) {
            char temp_pass[CONFIG_MAX_PASSWORD_LEN * 3];
            strncpy(temp_pass, pass_start, pass_len);
            temp_pass[pass_len] = '\0';
            url_decode(password, temp_pass);
        } else {
            password[0] = '\0'; // Empty password
        }
    } else {
        password[0] = '\0'; // No password field
    }
    // Trim trailing whitespace from SSID
    int ssid_trim_pos = strlen(ssid) - 1;
    while (ssid_trim_pos >= 0 && (ssid[ssid_trim_pos] == ' ' || ssid[ssid_trim_pos] == '\t' || ssid[ssid_trim_pos] == '\r' || ssid[ssid_trim_pos] == '\n')) {
        ssid[ssid_trim_pos] = '\0';
        ssid_trim_pos--;
    }
    // Trim trailing whitespace from password
    int pass_trim_pos = strlen(password) - 1;
    while (pass_trim_pos >= 0 && (password[pass_trim_pos] == ' ' || password[pass_trim_pos] == '\t' || password[pass_trim_pos] == '\r' || password[pass_trim_pos] == '\n')) {
        password[pass_trim_pos] = '\0';
        pass_trim_pos--;
    }
    ESP_LOGI(TAG, "Parsed results (after trim) - SSID: '%s', Password: '%s'", ssid, password[0] ? "***" : "(empty)");
    return true;
} /**/


/**
 * Helper function to get form value by key
 */
static const char* get_form_value(const char *data, const char *key) {
    static char value[512];
    char search_key[64];
    snprintf(search_key, sizeof(search_key), "%s=", key);
    const char *start = strstr(data, search_key);
    if (!start) {
        return NULL;
    }
    start += strlen(search_key);
    const char *end = strchr(start, '&');
    size_t len = end ? (end - start) : strlen(start);
    if (len >= sizeof(value)) {
        len = sizeof(value) - 1;
    }
    strncpy(value, start, len);
    value[len] = '\0';
    // URL decode
    char decoded[512];
    url_decode(decoded, value);
    strncpy(value, decoded, sizeof(value) - 1);
    return value;
} /**/


/**
 * Helper function to send error page
 */
static esp_err_t send_error_page(httpd_req_t *req, const char *title, const char *message) {
    char html[1024];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;padding:20px;background:#202020;color:#fff}"
        "h1{color:#ff4444}</style></head><body>"
        "<h1>%s</h1>"
        "<p>%s</p>"
        "<a href='/' style='color:#4499ff'>Back to Setup</a>"
        "</body></html>",
        title, message);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_FAIL;
} /**/


/** 
 * Helper function to extract ID from URI
 */
static int extract_id_from_uri(const char *uri) {
    const char *last_slash = strrchr(uri, '/');
    if (!last_slash) {
        return -1;
    }
    return atoi(last_slash + 1);
} /**/


/**
 * DNS server task
 */
static void dns_server_task(void *pvParameters) {
    char rx_buffer[DNS_MAX_PACKET_SIZE];
    char tx_buffer[DNS_MAX_PACKET_SIZE];
    struct sockaddr_in dest_addr;
    // Create UDP socket
    g_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create DNS socket: errno %d", errno);
        g_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    // Bind to DNS port
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_PORT);
    int err = bind(g_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(g_dns_socket);
        g_dns_socket = -1;
        g_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "DNS server listening on port %d", DNS_PORT);
    while (1) {
        socklen_t socklen = sizeof(dest_addr);
        int len = recvfrom(g_dns_socket, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&dest_addr, &socklen);
        if (len < 0) {
            if (errno == EBADF) {
                // Socket closed, exit gracefully
                ESP_LOGI(TAG, "DNS socket closed, stopping DNS server");
                break;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            continue;
        }
        if (len < sizeof(dns_header_t)) {
            ESP_LOGW(TAG, "DNS packet too short: %d bytes", len);
            continue;
        }
        dns_header_t *header = (dns_header_t *)rx_buffer;
        ESP_LOGI(TAG, "DNS query received, ID: 0x%04x", ntohs(header->id));
        // Build DNS response, copy the query
        memcpy(tx_buffer, rx_buffer, len);
        dns_header_t *response_header = (dns_header_t *)tx_buffer;
        response_header->flags = htons(0x8180);  // Standard query response, no error
        response_header->answers = htons(1);     // One answer
        response_header->authority = 0;
        response_header->additional = 0;
        // Add the answer (A record pointing to 192.168.4.1)
        uint8_t *answer = (uint8_t *)(tx_buffer + len);
        // Name pointer to question (compression)
        *answer++ = 0xC0;
        *answer++ = 0x0C;
        // Type A (0x0001)
        *answer++ = 0x00;
        *answer++ = 0x01;
        // Class IN (0x0001)
        *answer++ = 0x00;
        *answer++ = 0x01;
        // TTL (60 seconds)
        *answer++ = 0x00;
        *answer++ = 0x00;
        *answer++ = 0x00;
        *answer++ = 0x3C;
        // Data length (4 bytes for IPv4)
        *answer++ = 0x00;
        *answer++ = 0x04;
        // IP address: 192.168.4.1
        *answer++ = 192;
        *answer++ = 168;
        *answer++ = 4;
        *answer++ = 1;
        int response_len = len + 16;  // Original query + answer record
        // Send response
        int sent = sendto(g_dns_socket, tx_buffer, response_len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (sent < 0) {
            ESP_LOGE(TAG, "Error sending DNS response: errno %d", errno);
        } else {
            ESP_LOGI(TAG, "DNS response sent: 192.168.4.1");
        }
    }
    close(g_dns_socket);
    g_dns_socket = -1;
    g_dns_task = NULL;
    vTaskDelete(NULL);
} /**/


/**
 * HTTP handlers
 */


/**
 * Profile add handler
 */
static esp_err_t profile_add_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Profile add request received");
    // Read form data
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error_page(req, "Error", "Failed to receive form data");
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Form data: %s", buf);
    // Check profile limit
    if (g_working_config.profile_count >= CONFIG_MAX_PROFILES) {
        return send_error_page(req, "Limit Reached", "Maximum 20 profiles allowed");
    }
    otp_profile_t new_profile = {0};
    bool parsed = false;
    // Check if URI is provided
    const char *uri = get_form_value(buf, "uri");
    if (uri && strlen(uri) > 0) {
        ESP_LOGI(TAG, "Parsing URI: %s", uri);
        // Parse URI
        if (parse_otpauth_uri(uri, &new_profile)) {
            parsed = true;
            ESP_LOGI(TAG, "URI parsed successfully");
        } else {
            return send_error_page(req, "Invalid URI", "Failed to parse otpauth:// URI. Please check the format.");
        }
    } else {
        // Manual entry
        ESP_LOGI(TAG, "Using manual entry");
        // Get form values and copy to local buffers IMMEDIATELY
        // (get_form_value uses a static buffer that gets overwritten on each call!)

        // Get and copy label
        const char *label_temp = get_form_value(buf, "manual_label");
        char label[CONFIG_MAX_PROFILE_NAME] = {0};
        if (label_temp) {
            strncpy(label, label_temp, CONFIG_MAX_PROFILE_NAME - 1);
        }
        // Get and copy issuer
        const char *issuer_temp = get_form_value(buf, "manual_issuer");
        char issuer[CONFIG_MAX_PROFILE_NAME] = {0};
        if (issuer_temp) {
            strncpy(issuer, issuer_temp, CONFIG_MAX_PROFILE_NAME - 1);
        }
        // Get and copy secret
        const char *secret_temp = get_form_value(buf, "manual_secret");
        char secret[CONFIG_MAX_SECRET_LEN] = {0};
        if (secret_temp) {
            strncpy(secret, secret_temp, CONFIG_MAX_SECRET_LEN - 1);
        }
        // Now safe to get other values
        const char *type_str = get_form_value(buf, "manual_type");
        const char *digits_str = get_form_value(buf, "manual_digits");
        const char *period_str = get_form_value(buf, "manual_period");
        ESP_LOGI(TAG, "Manual entry - label: '%s', issuer: '%s', secret: '%s'", label, issuer, secret);
        // If both label and secret are empty, user clicked button without filling anything
        // Just redirect back to the form without error (allows viewing current profiles)
        if (strlen(label) == 0 && strlen(secret) == 0) {
            ESP_LOGI(TAG, "Add profile clicked with empty fields - redirecting back");
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }
        // Validate required fields only if user started filling the form
        if (strlen(label) == 0) {
            return send_error_page(req, "Invalid Input", "Label is required");
        }
        if (strlen(secret) == 0) {
            return send_error_page(req, "Invalid Input", "Secret is required");
        }
        // Copy strings
        strncpy(new_profile.label, label, CONFIG_MAX_PROFILE_NAME - 1);
        if (strlen(issuer) > 0) {
            strncpy(new_profile.issuer, issuer, CONFIG_MAX_PROFILE_NAME - 1);
        }
        // Convert secret to uppercase, trim whitespace, and validate Base32
        char upper_secret[CONFIG_MAX_SECRET_LEN];
        strncpy(upper_secret, secret, CONFIG_MAX_SECRET_LEN - 1);
        upper_secret[CONFIG_MAX_SECRET_LEN - 1] = '\0';
        // Trim leading whitespace
        char *secret_start = upper_secret;
        while (*secret_start && (*secret_start == ' ' || *secret_start == '\t' || *secret_start == '\r' || *secret_start == '\n')) {
            secret_start++;
        }
        // Trim trailing whitespace
        int secret_len = strlen(secret_start);
        while (secret_len > 0 && (secret_start[secret_len - 1] == ' '  || secret_start[secret_len - 1] == '\t' ||
                                  secret_start[secret_len - 1] == '\r' || secret_start[secret_len - 1] == '\n')) {
            secret_start[secret_len - 1] = '\0';
            secret_len--;
        }
        // Convert to uppercase
        for (size_t i = 0; secret_start[i]; i++) {
            secret_start[i] = toupper(secret_start[i]);
        }
        ESP_LOGI(TAG, "Secret after trim/uppercase: '%s' (len=%d)", secret_start, strlen(secret_start));
        if (!is_valid_base32(secret_start)) {
            ESP_LOGE(TAG, "Base32 validation failed for secret: '%s'", secret_start);
            return send_error_page(req, "Invalid Secret",
                "Secret must be Base32 encoded (A-Z, 2-7, =). Only letters A-Z, digits 2-7 allowed.");
        }
        strncpy(new_profile.secret, secret_start, CONFIG_MAX_SECRET_LEN - 1);
        new_profile.secret[CONFIG_MAX_SECRET_LEN - 1] = '\0';
        // Parse type
        if (type_str && strcmp(type_str, "hotp") == 0) {
            new_profile.type = OTP_TYPE_HOTP;
            new_profile.counter = 0;
        } else {
            new_profile.type = OTP_TYPE_TOTP;
            new_profile.period = period_str ? atoi(period_str) : 30;
            if (new_profile.period <= 0) new_profile.period = 30;
        }
        // Parse digits
        new_profile.digits = (digits_str && strcmp(digits_str, "8") == 0) ? 8 : 6;
        // Generate default icon
        config_generate_default_icon(&new_profile);
        parsed = true;
    }
    if (parsed) {
        // Add to working config
        g_working_config.profiles[g_working_config.profile_count++] = new_profile;
        ESP_LOGI(TAG, "Profile added: '%s' (total: %d)", new_profile.label, g_working_config.profile_count);
        // Redirect back to main page
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    return send_error_page(req, "Error", "Failed to add profile");
} /**/


/**
 * Profile edit POST handler - show edit form or save changes
 */
static esp_err_t profile_edit_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Profile edit POST request");
    // Read POST data
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error_page(req, "Error", "Failed to read request data");
    }
    buf[ret] = '\0';
    // Extract ID from form data
    int id = -1;
    const char *id_start = strstr(buf, "id=");
    if (id_start) {
        id = atoi(id_start + 3);
    }
    if (id < 0 || id >= g_working_config.profile_count) {
        return send_error_page(req, "Invalid Profile", "Profile not found");
    }
    // Check if this is a save operation (has label field) or just showing form (only ID)
    const char *label_temp = get_form_value(buf, "label");
    if (label_temp && strlen(label_temp) > 0) {
        // SAVE OPERATION - update profile
        // Copy label to local buffer before calling get_form_value again (it uses static buffer)
        char label[CONFIG_MAX_PROFILE_NAME];
        strncpy(label, label_temp, CONFIG_MAX_PROFILE_NAME - 1);
        label[CONFIG_MAX_PROFILE_NAME - 1] = '\0';
        // Copy issuer to local buffer before calling get_form_value again (it uses static buffer)
        const char *issuer_temp = get_form_value(buf, "issuer");
        char issuer[CONFIG_MAX_PROFILE_NAME] = {0};
        if (issuer_temp && strlen(issuer_temp) > 0) {
            strncpy(issuer, issuer_temp, CONFIG_MAX_PROFILE_NAME - 1);
        }
        // Copy icon to local buffer and convert to uppercase
        const char *icon_temp = get_form_value(buf, "icon");
        char icon[3] = {0};
        if (icon_temp && strlen(icon_temp) > 0) {
            strncpy(icon, icon_temp, 2);
            // Convert to uppercase (A-Z, 0-9 only)
            for (size_t i = 0; icon[i]; i++) {
                icon[i] = toupper(icon[i]);
            }
        }
        // Now update the profile
        strncpy(g_working_config.profiles[id].label, label, CONFIG_MAX_PROFILE_NAME - 1);
        g_working_config.profiles[id].label[CONFIG_MAX_PROFILE_NAME - 1] = '\0';
        if (strlen(issuer) > 0) {
            strncpy(g_working_config.profiles[id].issuer, issuer, CONFIG_MAX_PROFILE_NAME - 1);
            g_working_config.profiles[id].issuer[CONFIG_MAX_PROFILE_NAME - 1] = '\0';
        } else {
            g_working_config.profiles[id].issuer[0] = '\0';
        }
        if (strlen(icon) > 0) {
            strncpy(g_working_config.profiles[id].icon, icon, 2);
            g_working_config.profiles[id].icon[2] = '\0';
        } else {
            // If empty, regenerate from label
            config_generate_default_icon(&g_working_config.profiles[id]);
        }
        ESP_LOGI(TAG, "Profile %d updated: label='%s', issuer='%s', icon='%s'", id, label, issuer, g_working_config.profiles[id].icon);
        // Redirect back to main page
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    } else {
        // SHOW EDIT FORM
        otp_profile_t *profile = &g_working_config.profiles[id];
        char html[2048];
        snprintf(html, sizeof(html),
            "<!DOCTYPE html><html><head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Edit Profile</title>"
            "<style>"
            "*{box-sizing:border-box}"
            "body{font-family:Arial;padding:20px;background:#202020;color:#fff;margin:0}"
            ".section{background:#2a2a2a;padding:15px;margin:10px 0;border-radius:8px}"
            "h1{margin:0 0 10px 0}"
            "input{width:100%%;padding:12px;margin:5px 0;font-size:16px;border-radius:4px;"
            "background:#1a1a1a;border:1px solid #404040;color:#fff}"
            "button{padding:12px;font-size:16px;border-radius:4px;background:#002f9f;"
            "color:#fff;border:none;cursor:pointer;width:100%%;margin:5px 0}"
            "button.secondary{background:#404040}"
            ".hint{font-size:12px;color:#888;margin:2px 0}"
            "</style></head><body>"
            "<h1>Edit Profile</h1>"
            "<div class='section'>"
            "<form method='POST' action='/profile/edit'>"
            "<input type='hidden' name='id' value='%d'>"
            "<label>Label:</label>"
            "<input name='label' value='%s' required>"
            "<div class='hint'>Display name (e.g., Google:user@gmail.com)</div>"
            "<label>Issuer:</label>"
            "<input name='issuer' value='%s'>"
            "<div class='hint'>Service provider (e.g., Google)</div>"
            "<label>Icon:</label>"
            "<input name='icon' value='%s' maxlength='2'>"
            "<div class='hint'>2 ASCII characters max (e.g., GO, GH, MS), no Unicode</div>"
            "<button type='submit'>Save Changes</button>"
            "<button type='button' class='secondary' onclick='location.href=\"/\"'>Cancel</button>"
            "</form>"
            "</div>"
            "</body></html>",
            id, profile->label, profile->issuer, profile->icon);
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html, strlen(html));
        return ESP_OK;
    }
} /**/


/**
 * Profile delete GET handler - show confirmation
 */
static esp_err_t profile_delete_get_handler(httpd_req_t *req) {
    int id = extract_id_from_uri(req->uri);
    ESP_LOGI(TAG, "Profile delete GET request for ID: %d", id);
    if (id < 0 || id >= g_working_config.profile_count) {
        return send_error_page(req, "Invalid Profile", "Profile not found");
    }
    otp_profile_t *profile = &g_working_config.profiles[id];
    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Confirm Delete</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{font-family:Arial;padding:20px;background:#202020;color:#fff;margin:0;text-align:center}"
        ".section{background:#2a2a2a;padding:20px;margin:20px auto;border-radius:8px;max-width:500px}"
        "h1{color:#ff4444;margin:0 0 10px 0}"
        "h2{margin:15px 0;color:#fff}"
        "p{font-size:16px;margin:10px 0}"
        "button{padding:12px 20px;font-size:16px;border-radius:4px;"
        "color:#fff;border:none;cursor:pointer;margin:5px;min-width:120px}"
        "button.danger{background:#9f0000}"
        "button.secondary{background:#404040}"
        "</style></head><body>"
        "<div class='section'>"
        "<h1>Confirm Delete</h1>"
        "<p>Are you sure you want to delete:</p>"
        "<h2>%s</h2>"
        "<p>This cannot be undone.</p>"
        "<form method='POST' action='/profile/delete/%d' style='display:inline'>"
        "<button type='submit' class='danger'>Yes, Delete</button>"
        "</form>"
        "<button type='button' class='secondary' onclick='location.href=\"/\"'>Cancel</button>"
        "</div>"
        "</body></html>",
        profile->label, id);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
} /**/


/**
 * Profile delete POST handler - perform deletion
 */
static esp_err_t profile_delete_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Profile delete POST request");
    // Read POST data
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error_page(req, "Error", "Failed to read request data");
    }
    buf[ret] = '\0';
    // Extract ID from form data (format: "id=0")
    int id = -1;
    const char *id_start = strstr(buf, "id=");
    if (id_start) {
        id = atoi(id_start + 3);
    }
    ESP_LOGI(TAG, "Deleting profile ID: %d", id);
    if (id < 0 || id >= g_working_config.profile_count) {
        return send_error_page(req, "Invalid Profile", "Profile not found");
    }
    // Shift array elements left to remove profile
    for (int i = id; i < g_working_config.profile_count - 1; i++) {
        g_working_config.profiles[i] = g_working_config.profiles[i + 1];
    }
    g_working_config.profile_count--;
    ESP_LOGI(TAG, "Profile %d deleted (remaining: %d)", id, g_working_config.profile_count);
    // Redirect back to main page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
} /**/


/**
 * Profile move up handler
 */
static esp_err_t profile_up_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Profile move up POST request");
    // Read POST data
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error_page(req, "Error", "Failed to read request data");
    }
    buf[ret] = '\0';
    // Extract ID from form data
    int id = -1;
    const char *id_start = strstr(buf, "id=");
    if (id_start) {
        id = atoi(id_start + 3);
    }
    ESP_LOGI(TAG, "Moving profile %d up", id);
    if (id > 0 && id < g_working_config.profile_count) {
        // Swap with previous
        otp_profile_t temp = g_working_config.profiles[id];
        g_working_config.profiles[id] = g_working_config.profiles[id - 1];
        g_working_config.profiles[id - 1] = temp;
        ESP_LOGI(TAG, "Profile %d moved up", id);
    }
    // Redirect back to main page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
} /**/


/**
 * Profile move down handler
 */
static esp_err_t profile_down_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Profile move down POST request");
    // Read POST data
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return send_error_page(req, "Error", "Failed to read request data");
    }
    buf[ret] = '\0';
    // Extract ID from form data
    int id = -1;
    const char *id_start = strstr(buf, "id=");
    if (id_start) {
        id = atoi(id_start + 3);
    }
    ESP_LOGI(TAG, "Moving profile %d down", id);
    if (id >= 0 && id < g_working_config.profile_count - 1) {
        // Swap with next
        otp_profile_t temp = g_working_config.profiles[id];
        g_working_config.profiles[id] = g_working_config.profiles[id + 1];
        g_working_config.profiles[id + 1] = temp;
        ESP_LOGI(TAG, "Profile %d moved down", id);
    }
    // Redirect back to main page
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
} /**/


/**
 * 
 */
static esp_err_t root_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Serving configuration page to: %s", req->uri);
    // Use working configuration (in-memory)
    if (!g_config_initialized) {
        ESP_LOGE(TAG, "Configuration not initialized");
        return ESP_FAIL;
    }
    // Build profile list HTML (allocate from heap to save stack space)
    char *profile_list = calloc(1, PORTAL_PROFILE_LIST_SIZE);
    if (!profile_list) {
        ESP_LOGE(TAG, "Failed to allocate profile_list buffer");
        return ESP_ERR_NO_MEM;
    }
    int offset = 0;
    for (int i = 0; i < g_working_config.profile_count; i++) {
        offset += snprintf(profile_list + offset, PORTAL_PROFILE_LIST_SIZE - offset,
            "<div class='profile-item'>"
            "<div class='profile-label'>%s</div>"
            "<div class='profile-issuer'>%s</div>"
            "<div class='profile-actions'>"
            "<button type='submit' form='up-%d' class='btn small secondary'>↑</button>"
            "<button type='submit' form='down-%d' class='btn small secondary'>↓</button>"
            "<button type='submit' form='edit-%d' class='btn small'>Edit</button>"
            "<button type='submit' form='delete-%d' class='btn small danger'>×</button>"
            "</div></div>",
            g_working_config.profiles[i].label,
            g_working_config.profiles[i].issuer[0] ? g_working_config.profiles[i].issuer : "(no issuer)",
            i, i, i, i);
    }
    // Build action forms (outside main form to avoid nesting, allocate from heap)
    char *action_forms = calloc(1, PORTAL_ACTION_FORMS_SIZE);
    if (!action_forms) {
        ESP_LOGE(TAG, "Failed to allocate action_forms buffer");
        free(profile_list);
        return ESP_ERR_NO_MEM;
    }
    offset = 0;
    for (int i = 0; i < g_working_config.profile_count; i++) {
        offset += snprintf(action_forms + offset, PORTAL_ACTION_FORMS_SIZE - offset,
            "<form id='up-%d' method='POST' action='/profile/up'>"
            "<input type='hidden' name='id' value='%d'>"
            "</form>"
            "<form id='down-%d' method='POST' action='/profile/down'>"
            "<input type='hidden' name='id' value='%d'>"
            "</form>"
            "<form id='edit-%d' method='POST' action='/profile/edit'>"
            "<input type='hidden' name='id' value='%d'>"
            "</form>"
            "<form id='delete-%d' method='POST' action='/profile/delete'>"
            "<input type='hidden' name='id' value='%d'>"
            "</form>",
            i, i, i, i, i, i, i, i);
    }
    // Build dynamic HTML with pre-populated values (allocate from heap)
    char *dynamic_html = malloc(PORTAL_DYNAMIC_HTML_SIZE);
    if (!dynamic_html) {
        ESP_LOGE(TAG, "Failed to allocate dynamic_html buffer");
        free(profile_list);
        free(action_forms);
        return ESP_ERR_NO_MEM;
    }
    snprintf(dynamic_html, PORTAL_DYNAMIC_HTML_SIZE,
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Keymaker Setup</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{font-family:Arial;padding:20px;background:#202020;color:#fff;margin:0}"
        "form{width:100%%;margin:0;padding:0}"
        ".section{background:#2a2a2a;padding:15px;margin:10px 0;border-radius:8px}"
        "h3{margin:0 0 10px 0;color:#fff;border-bottom:1px solid #404040;padding-bottom:8px}"
        "input,textarea{width:100%%;padding:12px;margin:5px 0;font-size:16px;border-radius:4px;"
        "background:#1a1a1a;border:1px solid #404040;color:#fff}"
        "textarea{min-height:120px;font-family:monospace}"
        ".button-group{display:grid;grid-template-columns:1fr;gap:10px;margin-top:10px}"
        "button,.btn{padding:12px;font-size:14px;border-radius:4px;background:#002f9f;color:#fff;"
        "border:none;cursor:pointer;text-decoration:none;text-align:center;display:inline-block}"
        "button.secondary,.btn.secondary{background:#404040}"
        "button.danger,.btn.danger{background:#9f0000}"
        "button.small,.btn.small{padding:6px 10px;font-size:12px;margin:0 2px}"
        ".profile-item{background:#1a1a1a;padding:10px;margin:5px 0;border-radius:4px}"
        ".profile-label{font-size:14px;line-height:1.4;margin-bottom:3px;word-wrap:break-word}"
        ".profile-issuer{font-size:11px;color:#888;margin-bottom:8px}"
        ".profile-actions{text-align:right;white-space:nowrap}"
        ".hint{font-size:12px;color:#888;margin:2px 0 5px 0}"
        "details{margin:10px 0}"
        "summary{cursor:pointer;padding:10px;background:#1a1a1a;border-radius:4px;margin-bottom:10px}"
        "label{display:block;margin:10px 0}"
        "</style></head><body>"
        "<h1>Keymaker Setup</h1>"
        "<form method='POST' action='/save'>"
        "<div class='section'>"
        "<h3>WiFi Settings</h3>"
        "<input name='ssid' placeholder='WiFi SSID' value='%s' required>"
        "<input name='password' type='password' placeholder='WiFi Password'>"
        "<div class='hint'>Leave password empty to keep current password</div>"
        "</div>"
        "<div class='section'>"
        "<h3>OTP Profiles (%d/%d)</h3>"
        "%s"
        "</div>"
        "<h3 style='margin-top:20px;padding-top:15px;border-top:1px solid #404040'>Add New Profile</h3>"
        "<textarea name='uri' placeholder='Paste otpauth:// URI here (from Google Lens or QR code)'></textarea>"
        "<details>"
        "<summary>Or enter manually ▼</summary>"
        "<input name='manual_label' placeholder='Label (e.g., Google:user@gmail.com)'>"
        "<input name='manual_issuer' placeholder='Issuer (e.g., Google)'>"
        "<input name='manual_secret' placeholder='Secret (Base32)'>"
        "<div class='hint'>Secret must be Base32 encoded (A-Z, 2-7)</div>"
        "<label><input type='radio' name='manual_type' value='totp' checked> TOTP (time-based)</label>"
        "<label><input type='radio' name='manual_type' value='hotp'> HOTP (counter-based)</label>"
        "<br><label><input type='radio' name='manual_digits' value='6' checked> 6 digits</label>"
        "<label><input type='radio' name='manual_digits' value='8'> 8 digits</label>"
        "<br><label>Period (seconds): <input type='number' name='manual_period' value='30' style='width:80px'></label>"
        "</details>"
        "<button type='submit' formaction='/profile/add' class='secondary'>+ Add Profile</button>"
        "</div>"
        "<div class='section'>"
        "<div class='button-group'>"
        "<button type='button' class='secondary' onclick='location.href=\"/calibrate\"'>Calibrate Touch Screen</button>"
        "<button type='submit'>Save Settings</button>"
        "<button type='button' onclick='location.href=\"/cancel\"'>Close without Saving</button>"
        "</div>"
        "</div>"
        "</form>"
        "%s"
        "</body></html>",
        (g_config_initialized && strlen(g_working_config.wifi_ssid) > 0) ? g_working_config.wifi_ssid : "",
        g_working_config.profile_count, CONFIG_MAX_PROFILES,
        g_working_config.profile_count > 0 ? profile_list : "<p style='color:#888'>No profiles yet</p>",
        action_forms
    );
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_send(req, dynamic_html, strlen(dynamic_html));
    // Free heap-allocated buffers
    free(profile_list);
    free(action_forms);
    free(dynamic_html);
    return ESP_OK;
} /**/


/**
 * Captive portal detection handler
 * Android/iOS check these URLs to detect captive portals
 */
static esp_err_t captive_portal_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive portal check from: %s", req->uri);
    // Redirect to main page instead of returning 204, this
    // triggers the "Sign in to network" notification
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
} /**/


/**
 * Handler function for saving data after http POST
 */
static esp_err_t save_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Received configuration save request");
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "Form data received: %s", buf);
    // Parse form data
    char ssid[CONFIG_MAX_SSID_LEN] = {0};
    char password[CONFIG_MAX_PASSWORD_LEN] = {0};
    if (!parse_form_data(buf, ssid, password)) {
        ESP_LOGE(TAG, "Failed to parse form data");
        const char *resp = "<!DOCTYPE html><html><body>"
            "<h1>Error</h1>"
            "<p>Failed to parse configuration. Please try again.</p>"
            "<a href='/'>Back</a>"
            "</body></html>";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Parsed WiFi config - SSID: '%s' (len=%d), Password: '%s' (len=%d)", ssid, strlen(ssid), password[0] ? "***" : "(empty)", strlen(password));
    // Update SSID in working config (always from form)
    strncpy(g_working_config.wifi_ssid, ssid, CONFIG_MAX_SSID_LEN - 1);
    // Update password only if provided, otherwise keep existing
    if (strlen(password) > 0) {
        // New password provided, update it
        strncpy(g_working_config.wifi_password, password, CONFIG_MAX_PASSWORD_LEN - 1);
        ESP_LOGI(TAG, "Password updated");
    } else if (strlen(g_working_config.wifi_password) > 0) {
        // Password field empty, keep existing password
        ESP_LOGI(TAG, "Password field empty, keeping existing password");
    } else {
        // No existing password and none provided
        g_working_config.wifi_password[0] = '\0';
        ESP_LOGI(TAG, "No password set");
    }
    // Save entire working config to NVS (WiFi + all profiles)
    ESP_LOGI(TAG, "Attempting to save config to NVS (WiFi + %d profiles)...", g_working_config.profile_count);
    esp_err_t save_ret = config_save(&g_working_config);
    if (save_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(save_ret));
        const char *resp = "<!DOCTYPE html><html><body>"
            "<h1>Error</h1>"
            "<p>Failed to save configuration. Please try again.</p>"
            "<a href='/'>Back</a>"
            "</body></html>";
        httpd_resp_send(req, resp, strlen(resp));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Configuration saved successfully to NVS!");
    // Set exit flag to signal main task to close setup
    g_exit_requested = true;
    // Send success response
    const char *resp = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;padding:20px;background:#202020;color:#fff;text-align:center}"
        "h1{color:#00ff00}p{font-size:18px;margin:15px 0}</style></head><body>"
        "<h1>✓ Settings Saved ✓</h1>"
        "<p>Your WiFi credentials have been saved successfully</p>"
        "<p><strong>The captive portal is now closing</strong></p>"
        "<p>You can close this browser window</p>"
        "</body></html>";
    httpd_resp_send(req, resp, strlen(resp));
    ESP_LOGI(TAG, "WiFi configuration saved successfully, exit requested");
    return ESP_OK;
} /**/


/**
 * Setup canceled function handler
 */
static esp_err_t cancel_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Configuration cancelled");
    // Set exit flag to signal main task to close setup
    g_exit_requested = true;
    const char *resp = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;padding:20px;background:#202020;color:#fff;text-align:center}"
        "h1{color:#ffaa00}p{font-size:18px;margin:15px 0}</style></head><body>"
        "<h1>Setup Cancelled</h1>"
        "<p>No changes were saved</p>"
        "<p><strong>The captive portal is now closing</strong></p>"
        "<p>You can close this browser window</p>"
        "</body></html>";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
} /**/


/**
 * Calibration function handler
 */
static esp_err_t calibrate_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Screen calibration requested");
    // Clear existing calibration from NVS
    esp_err_t ret = touch_cal_clear();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Cleared existing calibration from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to clear calibration (may not exist): %s", esp_err_to_name(ret));
    }
    // Set calibration request flag
    g_calibration_requested = true;
    const char *resp = "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{font-family:Arial;padding:20px;background:#202020;color:#fff}"
        "h1{color:#00ff00}</style></head><body>"
        "<h1>Calibration Started</h1>"
        "<h3><p><strong>Look at the device screen now!</strong></p>"
        "<p>Follow these steps:</p>"
        "<ol>"
        "<li>Touch each RED target that appears on the screen (4 corners)</li>"
        "<li>After calibration completes, the device will automatically return to the main screen</li>"
        "<li>Setup mode will close automatically</li>"
        "</ol></h3>"
        "<p>You can close this page.</p>"
        "</body></html>";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
} /**/


static httpd_uri_t uri_root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_save = {
    .uri = "/save",
    .method = HTTP_POST,
    .handler = save_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_cancel = {
    .uri = "/cancel",
    .method = HTTP_GET,
    .handler = cancel_get_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_calibrate = {
    .uri = "/calibrate",
    .method = HTTP_GET,
    .handler = calibrate_get_handler,
    .user_ctx = NULL
};
/**
 * Wildcard GET handler for /profile/* paths
 */
static esp_err_t profile_wildcard_get_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    ESP_LOGI(TAG, "Profile GET: %s", uri);
    // GET /profile/delete/:id (confirmation page)
    if (strncmp(uri, "/profile/delete/", 16) == 0) {
        return profile_delete_get_handler(req);
    }
    // No match
    return send_error_page(req, "404 Not Found", "Profile endpoint not found");
} /**/


/**
 * Wildcard POST handler for /profile/* paths
 */
static esp_err_t profile_wildcard_post_handler(httpd_req_t *req) {
    const char *uri = req->uri;
    ESP_LOGI(TAG, "Profile POST: %s", uri);
    // No wildcard routes needed anymore - all using specific endpoints
    return send_error_page(req, "404 Not Found", "Profile POST endpoint not found");
} /**/


/**
 * Explicit handlers for profile endpoints
 */
static httpd_uri_t uri_profile_add = {
    .uri = "/profile/add",
    .method = HTTP_POST,
    .handler = profile_add_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_delete = {
    .uri = "/profile/delete",
    .method = HTTP_POST,
    .handler = profile_delete_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_edit = {
    .uri = "/profile/edit",
    .method = HTTP_POST,
    .handler = profile_edit_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_up = {
    .uri = "/profile/up",
    .method = HTTP_POST,
    .handler = profile_up_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_down = {
    .uri = "/profile/down",
    .method = HTTP_POST,
    .handler = profile_down_post_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_wildcard_get = {
    .uri = "/profile/*",
    .method = HTTP_GET,
    .handler = profile_wildcard_get_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_profile_wildcard_post = {
    .uri = "/profile/*",
    .method = HTTP_POST,
    .handler = profile_wildcard_post_handler,
    .user_ctx = NULL
};
// Captive portal detection endpoints
static httpd_uri_t uri_generate_204 = {
    .uri = "/generate_204",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_gen_204 = {
    .uri = "/gen_204",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_hotspot_detect = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};
static httpd_uri_t uri_success_txt = {
    .uri = "/success.txt",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};
/**
 * Wildcard handler - catch all other GET requests
 */
esp_err_t portal_start(void) {
    if (g_portal_running) {
        ESP_LOGW(TAG, "Portal already running");
        return ESP_OK;
    }
    if (g_portal_stopping) {
        ESP_LOGW(TAG, "Portal is still stopping, please wait");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Starting captive portal");
    // Clear exit and calibration flags from previous session
    g_exit_requested = false;
    g_calibration_requested = false;
    // Load configuration into working memory
    memset(&g_working_config, 0, sizeof(keymaker_config_t));
    esp_err_t ret = config_load(&g_working_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No existing configuration, starting with defaults");
    }
    g_config_initialized = true;
    // Generate random SSID suffix
    srand(time(NULL));
    int random_num = rand() % 10000;
    snprintf(g_ssid, sizeof(g_ssid), "keymaker-%04d", random_num);
    // Create default WiFi AP netif
    g_ap_netif = esp_netif_create_default_wifi_ap();
    if (!g_ap_netif) {
        ESP_LOGE(TAG, "Failed to create AP network interface");
        return ESP_FAIL;
    }
    // Initialize WiFi in AP mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(g_ssid),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strcpy((char *)wifi_config.ap.ssid, g_ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi AP started: SSID='%s'", g_ssid);
    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = PORTAL_HTTP_STACK_SIZE;  // Buffers now allocated from heap, not stack
    config.max_uri_handlers = 20;                // Increase from default 8 to support all our handlers
    ESP_LOGI(TAG, "Starting HTTP server on port %d with stack size %d", config.server_port, config.stack_size);
    if (httpd_start(&g_server, &config) == ESP_OK) {
        // Register captive portal detection handlers FIRST
        httpd_register_uri_handler(g_server, &uri_generate_204);
        httpd_register_uri_handler(g_server, &uri_gen_204);
        httpd_register_uri_handler(g_server, &uri_hotspot_detect);
        httpd_register_uri_handler(g_server, &uri_success_txt);
        // Register main app handlers
        httpd_register_uri_handler(g_server, &uri_root);
        httpd_register_uri_handler(g_server, &uri_save);
        httpd_register_uri_handler(g_server, &uri_cancel);
        httpd_register_uri_handler(g_server, &uri_calibrate);
        // Register profile handlers (specific before wildcard)
        httpd_register_uri_handler(g_server, &uri_profile_add);
        httpd_register_uri_handler(g_server, &uri_profile_delete);
        httpd_register_uri_handler(g_server, &uri_profile_edit);
        httpd_register_uri_handler(g_server, &uri_profile_up);
        httpd_register_uri_handler(g_server, &uri_profile_down);
        httpd_register_uri_handler(g_server, &uri_profile_wildcard_get);
        httpd_register_uri_handler(g_server, &uri_profile_wildcard_post);
        // NOTE: Wildcard handlers removed - they interfere with ESP-IDF routing
        // All unmatched requests will get ESP-IDF's default 404
        ESP_LOGI(TAG, "HTTP server started with captive portal detection");
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    // Start DNS server task
    BaseType_t task_created = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &g_dns_task);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        return ESP_FAIL;
    }
    g_portal_running = true;
    ESP_LOGI(TAG, "Captive portal running: %s", g_url);
    return ESP_OK;
} /**/


/**
 * Closing captive portal
 */
esp_err_t portal_stop(void) {
    if (!g_portal_running) {
        ESP_LOGW(TAG, "Portal not running");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Stopping captive portal");
    g_portal_stopping = true;  // Set flag to prevent restart attempts
    // Stop DNS server
    if (g_dns_socket >= 0) {
        close(g_dns_socket);
        g_dns_socket = -1;
        ESP_LOGI(TAG, "DNS socket closed");
    }
    // Wait for DNS task to finish
    if (g_dns_task) {
        int wait_count = 0;
        while (g_dns_task && wait_count < 10) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        ESP_LOGI(TAG, "DNS server stopped");
    }
    // Stop HTTP server
    if (g_server) {
        httpd_stop(g_server);
        g_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    // Stop WiFi AP (this takes time)
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "WiFi AP stopped");
    // Destroy the network interface to prevent duplicate key error on next start
    if (g_ap_netif) {
        esp_netif_destroy(g_ap_netif);
        g_ap_netif = NULL;
        ESP_LOGI(TAG, "AP network interface destroyed");
    }
    g_portal_running = false;
    g_portal_stopping = false;  // Clear flag - fully stopped now
    // WiFi connection will be handled by main loop after portal exit
    ESP_LOGI(TAG, "Portal fully stopped - WiFi connection will be handled by main application");
    return ESP_OK;
} /**/


/**
 * Returning SSID for captive portal
 */
const char* portal_get_ssid(void) {
    return g_ssid;
} /**/

/**
 * Returning captive portal URL
 */
const char* portal_get_url(void) {
    return g_url;
} /**/

/**
 * property: captive portal running (true/false)
 */
bool portal_is_running(void) {
    return g_portal_running;
} /**/

/**
 * property: captive portal stopping (true/false)
 */
bool portal_is_stopping(void) {
    return g_portal_stopping;
} /**/

/**
 * property: Calibration requested (true/false)
 */
bool portal_calibration_requested(void) {
    return g_calibration_requested;
} /**/

/**
 * Method: clearing calibration action requested
 */
void portal_calibration_clear(void) {
    g_calibration_requested = false;
    ESP_LOGI(TAG, "Calibration request flag cleared");
} /**/

/**
 * property: captive portal exit requested (true/false)
 */
bool portal_exit_requested(void) {
    return g_exit_requested;
} /**/


/**
 * Method: captive portal exit aborting
 */
void portal_exit_clear(void) {
    g_exit_requested = false;
    ESP_LOGI(TAG, "Exit request flag cleared");
} /**/

/**
 * Method: captive portal exit set
 */
void portal_request_exit(void) {
    g_exit_requested = true;
    ESP_LOGI(TAG, "Exit requested");
} /**/
