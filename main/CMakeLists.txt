idf_component_register(SRCS "main.c"
                            "touch_calibration.c"
                            "display_main.c"
                            "display_setup.c"
                            "display_pin.c"
                            "portal.c"
                            "config.c"
                            "crypto.c"
                            "pin_manager.c"
                            "otp_uri.c"
                            "totp.c"
                            "time_sync.c"
                            "wifi_error.c"
                    INCLUDE_DIRS "."
                    REQUIRES mbedtls json nvs_flash
                             esp_lcd espressif__esp_lcd_touch esp_lcd_touch_xpt2046
                             esp_netif esp_wifi esp_event lvgl__lvgl
                             espressif__esp_lcd_ili9341 esp_http_server)
