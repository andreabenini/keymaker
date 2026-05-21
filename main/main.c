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

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "display_main.h"
#include "display_setup.h"
#include "display_pin.h"
#include "config.h"
#include "crypto.h"
#include "pin_manager.h"
#include "touch_calibration.h"
#include "portal.h"
#include "time_sync.h"

static const char *TAG = "keymaker";

// Display SPI pins (from host-monitor.yaml)
#define LCD_HOST               SPI2_HOST
#define PIN_NUM_LCD_SCLK       14
#define PIN_NUM_LCD_MOSI       13
#define PIN_NUM_LCD_MISO       12
#define PIN_NUM_LCD_CS         15
#define PIN_NUM_LCD_DC         2
#define PIN_NUM_LCD_RST        -1
#define PIN_NUM_BK_LIGHT       21

// Touch SPI pins (from host-monitor.yaml)
#define TOUCH_HOST             SPI3_HOST
#define PIN_NUM_TOUCH_SCLK     25
#define PIN_NUM_TOUCH_MOSI     32
#define PIN_NUM_TOUCH_MISO     39
#define PIN_NUM_TOUCH_CS       33
#define PIN_NUM_TOUCH_IRQ      36

// Display resolution
#define LCD_H_RES              320
#define LCD_V_RES              240
#define LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)

// LVGL settings
#define LVGL_TICK_PERIOD_MS    2
#define LVGL_BUFFER_HEIGHT     40

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_color_t *buf1 = NULL;
static lv_color_t *buf2 = NULL;

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;
static lv_disp_t *disp = NULL;

static SemaphoreHandle_t lvgl_mux = NULL;

// Global encryption key (derived from PIN on boot)
static uint8_t g_encryption_key[CRYPTO_KEY_SIZE];
static bool g_encryption_key_valid = false;

// LVGL flush callback
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

// LVGL input device read callback with calibration
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)drv->user_data;
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    // Minimum pressure threshold for touch registration (adjust based on testing)
    // XPT2046 reports values typically 0-4095, higher = more pressure
    // Increase if too sensitive, decrease if missing touches (200 seems to be a good default one)
    #define TOUCH_PRESSURE_THRESHOLD 150

    // Simple debouncing - require stable coordinates for multiple reads
    static int16_t last_x = -1, last_y = -1;
    static uint8_t stable_count = 0;
    #define TOUCH_STABILITY_THRESHOLD 1  // Require 1 stable read for faster button response
    #define TOUCH_JITTER_TOLERANCE 100   // Pixels of acceptable movement - higher = more tolerant

    esp_lcd_touch_read_data(touch);
    bool touched = esp_lcd_touch_get_coordinates(touch, touch_x, touch_y, touch_strength, &touch_cnt, 1);

    if (touched && touch_cnt > 0 && touch_strength[0] >= TOUCH_PRESSURE_THRESHOLD) {
        // Apply calibration transformation
        int16_t cal_x, cal_y;
        touch_cal_transform(touch_x[0], touch_y[0], &cal_x, &cal_y);

        // Check if coordinates are stable (debouncing)
        bool is_stable = false;
        if (last_x >= 0 && last_y >= 0) {
            int16_t dx = cal_x - last_x;
            int16_t dy = cal_y - last_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            if (dx <= TOUCH_JITTER_TOLERANCE && dy <= TOUCH_JITTER_TOLERANCE) {
                stable_count++;
                if (stable_count >= TOUCH_STABILITY_THRESHOLD) {
                    is_stable = true;
                }
            } else {
                stable_count = 0;
            }
        } else {
            stable_count = 0;
        }

        last_x = cal_x;
        last_y = cal_y;

        // Debug: log touch coordinates with pressure
        static uint32_t last_log_time = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_log_time > 500) {  // Log every 500ms max
            ESP_LOGI(TAG, "Touch: raw(%d,%d) -> cal(%d,%d), pressure=%d, stable=%d",
                     touch_x[0], touch_y[0], cal_x, cal_y, touch_strength[0], stable_count);
            last_log_time = now;
        }

        // Only report touch if stable (or on first touch to avoid lag)
        if (is_stable || stable_count == 0) {
            data->point.x = cal_x;
            data->point.y = cal_y;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            // Keep previous position while stabilizing
            data->state = LV_INDEV_STATE_PRESSED;
        }
    } else {
        // Touch released or pressure too low
        data->state = LV_INDEV_STATE_RELEASED;
        last_x = last_y = -1;
        stable_count = 0;
    }
}

// LVGL tick timer callback
static void lvgl_tick_timer_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        display_main_set_wifi_connecting();
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        display_main_set_wifi_status(false);
        time_sync_stop();
        display_main_set_wifi_connecting();
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));

        // Get WiFi info for display
        wifi_ap_record_t ap_info;
        char ssid[33] = {0};
        char ip_str[16];
        int8_t rssi = 0;

        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            memcpy(ssid, ap_info.ssid, sizeof(ssid) - 1);
            rssi = ap_info.rssi;
        }
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        display_main_set_wifi_info(ssid, ip_str, rssi);
        display_main_set_wifi_status(true);

        // Start NTP time synchronization
        time_sync_start();
    }
}

// LVGL task
static void lvgl_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Starting LVGL task");

    while (1) {
        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Keymaker init");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize configuration system
    ESP_ERROR_CHECK(config_init());

    // Initialize networking
    ESP_LOGI(TAG, "Initializing network stack");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register WiFi event handlers (WiFi will be initialized after PIN unlock)
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Initialize backlight
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(PIN_NUM_BK_LIGHT, 0); // Off initially

    // Initialize Display SPI bus
    spi_bus_config_t lcd_bus_cfg = {
        .sclk_io_num = PIN_NUM_LCD_SCLK,
        .mosi_io_num = PIN_NUM_LCD_MOSI,
        .miso_io_num = PIN_NUM_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &lcd_bus_cfg, SPI_DMA_CH_AUTO));

    // Initialize LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // Initialize LCD panel (panel order *_BGR|*_RGB)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Initialize Touch SPI bus
    spi_bus_config_t touch_bus_cfg = {
        .sclk_io_num = PIN_NUM_TOUCH_SCLK,
        .mosi_io_num = PIN_NUM_TOUCH_MOSI,
        .miso_io_num = PIN_NUM_TOUCH_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_HOST, &touch_bus_cfg, SPI_DMA_CH_AUTO));

    // Initialize touch panel IO
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t touch_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &touch_io_config, &touch_io_handle));

    // Initialize touch controller
    esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_NUM_TOUCH_IRQ,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(touch_io_handle, &touch_config, &touch_handle));

    // Turn on backlight
    gpio_set_level(PIN_NUM_BK_LIGHT, 1);
    ESP_LOGI(TAG, "Display and touch initialized");

    // Initialize LVGL
    lv_init();

    // Create LVGL mutex
    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);

    // Allocate LVGL draw buffers
    size_t buffer_size = LCD_H_RES * LVGL_BUFFER_HEIGHT * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    assert(buf1);
    buf2 = heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    assert(buf2);

    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LVGL_BUFFER_HEIGHT);

    // Register display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    disp = lv_disp_drv_register(&disp_drv);

    // Register input device driver
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = touch_handle;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    // Set faster polling for better touch responsiveness (default is 30ms)
    lv_timer_set_period(indev->driver->read_timer, 10);

    // Create LVGL tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "LVGL initialized");

    // Start LVGL task (increased stack for WiFi reinitialization)
    xTaskCreate(lvgl_task, "lvgl_task", 8192, NULL, 5, NULL);

    // Initialize touch calibration system
    touch_cal_set_handle(touch_handle);
    ESP_ERROR_CHECK(touch_cal_init());

    // Check if calibration exists, run if not
    if (!touch_cal_exists()) {
        ESP_LOGW(TAG, "No touch calibration found - starting calibration");

        if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
            touch_cal_run(disp);
            xSemaphoreGive(lvgl_mux);
        }

        ESP_LOGI(TAG, "Touch calibration complete");
    } else {
        ESP_LOGI(TAG, "Touch calibration loaded from NVS");
    }

    // Unlock device with PIN and derive encryption key
    ESP_LOGI(TAG, "Starting PIN unlock");
    ret = pin_manager_unlock(disp, panel_handle, touch_handle, lvgl_mux, g_encryption_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PIN unlock failed: %s", esp_err_to_name(ret));
        esp_restart();
    }
    g_encryption_key_valid = true;

    // Set encryption key in config module
    config_set_encryption_key(g_encryption_key);

    ESP_LOGI(TAG, "Device unlocked successfully");

    // Now that encryption key is set, try to load WiFi credentials and connect
    keymaker_config_t unlocked_config = {0};
    if (config_load(&unlocked_config) == ESP_OK && strlen(unlocked_config.wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Found WiFi credentials after unlock, connecting to: %s", unlocked_config.wifi_ssid);

        // Create station network interface
        esp_netif_create_default_wifi_sta();

        // Initialize WiFi
        wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

        // Configure WiFi
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, unlocked_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, unlocked_config.wifi_password, sizeof(wifi_config.sta.password));

        if (strlen(unlocked_config.wifi_password) > 0) {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        }

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "WiFi connecting to: %s", unlocked_config.wifi_ssid);
    } else {
        ESP_LOGI(TAG, "No saved WiFi credentials found after unlock");
    }

    // Create main UI
    if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
        display_main_create(disp, panel_handle, touch_handle);
        xSemaphoreGive(lvgl_mux);
    }

    ESP_LOGI(TAG, "Initialization completed");

    // Main task now just idles (LVGL task handles UI updates)
    // but also checks for calibration requests from the captive portal
    while (1) {
        // Check if calibration was requested via the web interface
        if (portal_calibration_requested()) {
            ESP_LOGI(TAG, "Calibration requested from captive portal");

            if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                // Run calibration procedure
                touch_cal_run(disp);

                // Show a message telling user to return to main screen
                lv_obj_t *scr = lv_disp_get_scr_act(disp);
                lv_obj_clean(scr);
                lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

                lv_obj_t *msg = lv_label_create(scr);
                lv_label_set_text(msg, "Calibration Saved!\n\nReturning to main screen...");
                lv_obj_set_style_text_color(msg, lv_color_hex(0x00FF00), 0);
                lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);
                lv_refr_now(disp);

                xSemaphoreGive(lvgl_mux);
            }

            // Wait 2 seconds to show the message (and for WiFi to reconnect)
            vTaskDelay(pdMS_TO_TICKS(2000));

            // Stop the captive portal and return to normal operation
            ESP_LOGI(TAG, "Stopping captive portal after calibration");
            portal_stop();

            // Return to main screen - create a fresh screen and rebuild
            if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "DEBUG: Creating new screen for main UI after calibration");

                // Create a completely new screen object
                lv_obj_t *new_scr = lv_obj_create(NULL);
                lv_obj_set_style_bg_color(new_scr, lv_color_hex(0x101010), 0);
                ESP_LOGI(TAG, "DEBUG: New screen created: %p", new_scr);

                // Load the new screen (this will delete the old setup screen)
                lv_disp_load_scr(new_scr);
                lv_obj_t *active = lv_disp_get_scr_act(disp);
                ESP_LOGI(TAG, "DEBUG: Active screen after load: %p", active);

                // Create main UI on the fresh screen
                ESP_LOGI(TAG, "DEBUG: Calling display_main_create...");
                display_main_create(disp, panel_handle, touch_handle);
                ESP_LOGI(TAG, "DEBUG: Main UI created successfully");

                xSemaphoreGive(lvgl_mux);
            }

            // Clear the request flag
            portal_calibration_clear();
            ESP_LOGI(TAG, "Calibration complete, returned to main screen");
        }

        // Check if exit was requested (save or cancel button pressed)
        if (portal_exit_requested()) {
            ESP_LOGI(TAG, "Portal exit requested (save/cancel)");

            // Stop the captive portal and return to normal operation
            ESP_LOGI(TAG, "Stopping captive portal");
            portal_stop();

            // Return to main screen - create a fresh screen and rebuild
            if (xSemaphoreTake(lvgl_mux, portMAX_DELAY) == pdTRUE) {
                // Create a completely new screen object
                lv_obj_t *new_scr = lv_obj_create(NULL);
                lv_obj_set_style_bg_color(new_scr, lv_color_hex(0x101010), 0);

                // Load the new screen (this will delete the old setup screen)
                lv_disp_load_scr(new_scr);

                // Create main UI on the fresh screen
                display_main_create(disp, panel_handle, touch_handle);
                xSemaphoreGive(lvgl_mux);
            }

            // Clear the request flag
            portal_exit_clear();
            ESP_LOGI(TAG, "Exited setup mode, returned to main screen");

            // Check if WiFi credentials were saved and start WiFi if needed
            keymaker_config_t portal_config = {0};
            ESP_LOGI(TAG, "Checking for WiFi credentials after portal exit");
            if (config_load(&portal_config) == ESP_OK && strlen(portal_config.wifi_ssid) > 0) {
                ESP_LOGI(TAG, "Found WiFi credentials: SSID=%s", portal_config.wifi_ssid);

                // Check if WiFi is already initialized (esp_wifi_stop returns ESP_ERR_WIFI_NOT_INIT if not)
                esp_err_t wifi_status = esp_wifi_stop();
                ESP_LOGI(TAG, "WiFi stop status: %s (0x%x)", esp_err_to_name(wifi_status), wifi_status);

                if (wifi_status == ESP_ERR_WIFI_NOT_INIT) {
                    // WiFi not initialized yet - start it now
                    ESP_LOGI(TAG, "WiFi not initialized - starting WiFi for the first time");

                    // Check if STA network interface already exists (it might from initial boot)
                    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                    if (!sta_netif) {
                        // Create station network interface only if it doesn't exist
                        sta_netif = esp_netif_create_default_wifi_sta();
                        ESP_LOGI(TAG, "Created WiFi STA interface: %p", sta_netif);
                    } else {
                        ESP_LOGI(TAG, "WiFi STA interface already exists: %p", sta_netif);
                    }

                    // Initialize WiFi
                    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
                    esp_err_t init_ret = esp_wifi_init(&wifi_cfg);
                    ESP_LOGI(TAG, "WiFi init result: %s", esp_err_to_name(init_ret));
                    ESP_ERROR_CHECK(init_ret);

                    // Configure WiFi
                    wifi_config_t wifi_config = {0};
                    strncpy((char *)wifi_config.sta.ssid, portal_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
                    strncpy((char *)wifi_config.sta.password, portal_config.wifi_password, sizeof(wifi_config.sta.password));

                    if (strlen(portal_config.wifi_password) > 0) {
                        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                    } else {
                        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
                    }

                    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                    ESP_LOGI(TAG, "Set WiFi mode to STA");
                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                    ESP_LOGI(TAG, "Set WiFi config");
                    esp_err_t start_ret = esp_wifi_start();
                    ESP_LOGI(TAG, "WiFi start result: %s", esp_err_to_name(start_ret));
                    ESP_ERROR_CHECK(start_ret);

                    ESP_LOGI(TAG, "WiFi connecting to: %s", portal_config.wifi_ssid);
                } else if (wifi_status == ESP_OK) {
                    // WiFi was running - restart it to pick up new credentials
                    ESP_LOGI(TAG, "WiFi was running - restarting with new credentials");

                    wifi_config_t wifi_config = {0};
                    strncpy((char *)wifi_config.sta.ssid, portal_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
                    strncpy((char *)wifi_config.sta.password, portal_config.wifi_password, sizeof(wifi_config.sta.password));

                    if (strlen(portal_config.wifi_password) > 0) {
                        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                    } else {
                        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
                    }

                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                    ESP_LOGI(TAG, "Set WiFi config");
                    esp_err_t start_ret = esp_wifi_start();
                    ESP_LOGI(TAG, "WiFi restart result: %s", esp_err_to_name(start_ret));
                    ESP_ERROR_CHECK(start_ret);

                    ESP_LOGI(TAG, "WiFi reconnecting to: %s", portal_config.wifi_ssid);
                } else {
                    ESP_LOGW(TAG, "WiFi stop returned unexpected status: %s", esp_err_to_name(wifi_status));
                }
            } else {
                ESP_LOGI(TAG, "No WiFi credentials found after portal exit");
            }
        }

        // Check if WiFi reconnect was requested from UI
        if (display_main_check_wifi_reconnect_requested()) {
            ESP_LOGI(TAG, "WiFi reconnect requested from UI");

            // Load WiFi credentials
            keymaker_config_t reconnect_config = {0};
            if (config_load(&reconnect_config) == ESP_OK && strlen(reconnect_config.wifi_ssid) > 0) {
                ESP_LOGI(TAG, "Performing full WiFi reconnect to: %s", reconnect_config.wifi_ssid);

                // Full WiFi deinit/reinit cycle
                esp_err_t stop_ret = esp_wifi_stop();
                if (stop_ret == ESP_OK) {
                    esp_wifi_deinit();
                    ESP_LOGI(TAG, "WiFi deinitialized for fresh start");
                }

                // Small delay to let WiFi fully clean up
                vTaskDelay(pdMS_TO_TICKS(500));

                // Check if STA network interface exists
                esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (!sta_netif) {
                    sta_netif = esp_netif_create_default_wifi_sta();
                    ESP_LOGI(TAG, "Created WiFi STA interface for reconnect");
                }

                // Reinitialize WiFi
                wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
                ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

                // Configure WiFi
                wifi_config_t wifi_config = {0};
                strncpy((char *)wifi_config.sta.ssid, reconnect_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
                strncpy((char *)wifi_config.sta.password, reconnect_config.wifi_password, sizeof(wifi_config.sta.password));

                if (strlen(reconnect_config.wifi_password) > 0) {
                    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                } else {
                    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
                }

                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_start());

                ESP_LOGI(TAG, "WiFi reconnect initiated to: %s", reconnect_config.wifi_ssid);
            } else {
                ESP_LOGW(TAG, "No WiFi credentials found for reconnect");
                // Set back to disconnected since we can't reconnect
                display_main_set_wifi_status(false);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
