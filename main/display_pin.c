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
#include "display_pin.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display_pin";

// Display resolution (imported from main)
#define LCD_H_RES              320
#define LCD_V_RES              240

// Global UI elements
static lv_obj_t *g_pin_screen = NULL;
static lv_obj_t *g_pin_display_label = NULL;
static lv_obj_t *g_enter_btn = NULL;
static lv_obj_t *g_enter_label = NULL;

// PIN state
static char g_pin_buffer[PIN_MAX_LENGTH + 1] = {0};
static int g_pin_length = 0;
static bool g_pin_complete = false;

// Display and touch handles
static lv_disp_t *g_disp = NULL;
static esp_lcd_panel_handle_t g_panel_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;

// Rotation state
static bool g_is_portrait = false;  // false = landscape, true = portrait
static int g_current_width = LCD_H_RES;
static int g_current_height = LCD_V_RES;

// Forward declarations
static void update_pin_display(void);
static void update_enter_button(void);
static void number_btn_event_cb(lv_event_t *e);
static void backspace_btn_event_cb(lv_event_t *e);
static void enter_btn_event_cb(lv_event_t *e);
static void hamburger_btn_event_cb(lv_event_t *e);
static void rebuild_pin_screen(void);

/**
 * @brief Update the PIN display with asterisks
 */
static void update_pin_display(void) {
    char display_text[PIN_MAX_LENGTH + 1];
    memset(display_text, 0, sizeof(display_text));
    for (int i = 0; i < g_pin_length; i++) {
        display_text[i] = '*';
    }
    lv_label_set_text(g_pin_display_label, display_text);
} /**/


/**
 * Update ENTER button state based on PIN length
 */
static void update_enter_button(void) {
    if (g_pin_length >= PIN_MIN_LENGTH) {
        // Enable ENTER button - green with white symbol
        lv_obj_set_style_bg_color(g_enter_btn, lv_color_hex(PIN_BUTTON_ENTER_ENABLED_COLOR), 0);
        lv_obj_set_style_text_color(g_enter_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0); // White symbol (visible on green)
        lv_obj_clear_state(g_enter_btn, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Enter button ENABLED - disabled state cleared");
    } else {
        // Disable ENTER button - gray with white symbol
        lv_obj_set_style_bg_color(g_enter_btn, lv_color_hex(PIN_BUTTON_ENTER_DISABLED_COLOR), 0);
        lv_obj_set_style_text_color(g_enter_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0); // White symbol (visible on gray)
        lv_obj_add_state(g_enter_btn, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Enter button DISABLED - disabled state set");
    }
} /**/


/**
 * Number button click handler
 */
static void number_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    // Provide visual feedback on press/release
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);  // Lighter on press
        return;
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_BUTTON_BG_COLOR), 0);  // Back to normal
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    int digit = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Number button %d clicked (portrait=%d, width=%d, height=%d)", digit, g_is_portrait, g_current_width, g_current_height);
    // Check if we can add more digits
    if (g_pin_length >= PIN_MAX_LENGTH) {
        ESP_LOGW(TAG, "PIN already at max length (%d)", PIN_MAX_LENGTH);
        return;
    }
    // Add digit to PIN
    g_pin_buffer[g_pin_length] = '0' + digit;
    g_pin_length++;
    g_pin_buffer[g_pin_length] = '\0';
    ESP_LOGI(TAG, "PIN length: %d", g_pin_length);
    update_pin_display();
    update_enter_button();
} /**/


/**
 * Backspace button click handler
 */
static void backspace_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    // Provide visual feedback on press/release
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF4040), 0);  // Lighter red on press
        return;
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_BUTTON_BACKSPACE_COLOR), 0);  // Back to normal red
        return;
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    // Remove last digit if any
    if (g_pin_length > 0) {
        g_pin_length--;
        g_pin_buffer[g_pin_length] = '\0';
        ESP_LOGI(TAG, "Backspace - PIN length: %d", g_pin_length);
        update_pin_display();
        update_enter_button();
    }
} /**/


/**
 * Enter button click handler
 */
 static void enter_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    ESP_LOGI(TAG, "Enter button event: code=%d, pin_length=%d", code, g_pin_length);
    // Provide visual feedback on press/release (only when enabled)
    if (g_pin_length >= PIN_MIN_LENGTH) {
        if (code == LV_EVENT_PRESSED) {
            ESP_LOGI(TAG, "Enter button PRESSED (enabled)");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x40FF40), 0);  // Lighter green on press
            return;
        } else if (code == LV_EVENT_RELEASED) {
            ESP_LOGI(TAG, "Enter button RELEASED (enabled)");
            lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_BUTTON_ENTER_ENABLED_COLOR), 0);  // Back to normal green
            return;
        }
    }
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    // Only process if we have at least minimum length
    if (g_pin_length >= PIN_MIN_LENGTH) {
        ESP_LOGI(TAG, "PIN entry complete - length: %d", g_pin_length);
        g_pin_complete = true;
    }
} /**/


/**
 * Hamburger button click handler - toggles rotation
 */
static void hamburger_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    ESP_LOGI(TAG, "Hamburger button event: %d (portrait=%d)", code, g_is_portrait);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Hamburger button clicked - toggling rotation from %s to %s", g_is_portrait ? "portrait" : "landscape", g_is_portrait ? "landscape" : "portrait");
        // Toggle rotation state
        g_is_portrait = !g_is_portrait;
        if (g_is_portrait) {
            // Switch to portrait mode (270 degrees)
            ESP_LOGI(TAG, "Switching to portrait mode");
            g_current_width = LCD_V_RES;
            g_current_height = LCD_H_RES;
            lv_disp_set_rotation(g_disp, LV_DISP_ROT_270);
            esp_lcd_panel_swap_xy(g_panel_handle, true);
            esp_lcd_panel_mirror(g_panel_handle, true, true);
            // Keep touch in landscape calibration mode - LVGL will handle rotation, (Touch calibration was done in landscape, so keep those settings)
        } else {
            // Switch to landscape mode (0 degrees)
            ESP_LOGI(TAG, "Switching to landscape mode");
            g_current_width = LCD_H_RES;
            g_current_height = LCD_V_RES;
            lv_disp_set_rotation(g_disp, LV_DISP_ROT_NONE);
            esp_lcd_panel_swap_xy(g_panel_handle, false);
            esp_lcd_panel_mirror(g_panel_handle, true, false);
            // Touch stays in landscape calibration mode (no changes needed)
        }
        // Rebuild PIN screen with new dimensions
        rebuild_pin_screen();
    }
} /**/


/**
 * Rebuild the PIN screen with current dimensions
 */
static void rebuild_pin_screen(void) {
    // Preserve PIN state
    char saved_pin[PIN_MAX_LENGTH + 1];
    int saved_length = g_pin_length;
    bool saved_complete = g_pin_complete;
    strncpy(saved_pin, g_pin_buffer, sizeof(saved_pin));
    // Delete existing screen
    if (g_pin_screen) {
        lv_obj_del(g_pin_screen);
        g_pin_screen = NULL;
        g_pin_display_label = NULL;
        g_enter_btn = NULL;
        g_enter_label = NULL;
    }
    // Recreate screen (will use updated g_current_width/height)
    display_pin_create(g_disp, g_panel_handle, g_touch_handle);
    // Restore PIN state
    strncpy(g_pin_buffer, saved_pin, sizeof(g_pin_buffer));
    g_pin_length = saved_length;
    g_pin_complete = saved_complete;
    // Update display
    update_pin_display();
    update_enter_button();
} /**/


/**
 * PIN creation and display screen
 */
void display_pin_create(lv_disp_t *disp, esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle) {
    // Store handles (only on first call)
    ESP_LOGI(TAG, "DEBUG: *** display_pin_create() CALLED *** (this should only happen at startup!)");
    if (!g_disp) {
        g_disp = disp;
        g_panel_handle = panel_handle;
        g_touch_handle = touch_handle;
        // Initialize dimensions (landscape mode by default)
        g_current_width = LCD_H_RES;
        g_current_height = LCD_V_RES;
    }
    // Reset PIN state on each creation (allows re-entry for confirmation)
    memset(g_pin_buffer, 0, sizeof(g_pin_buffer));
    g_pin_length = 0;
    g_pin_complete = false;

    // Get active screen
    lv_obj_t *scr = lv_disp_get_scr_act(disp);

    // Set background to dark gray
    lv_obj_set_style_bg_color(scr, lv_color_hex(PIN_SCREEN_BACKGROUND_COLOR), 0);

    // Create main container - positioned at absolute (0,0) to eliminate any coordinate offset
    g_pin_screen = lv_obj_create(scr);
    lv_obj_set_size(g_pin_screen, g_current_width, g_current_height);
    lv_obj_set_pos(g_pin_screen, 0, 0);  // Absolute position at origin
    lv_obj_set_style_bg_color(g_pin_screen, lv_color_hex(PIN_SCREEN_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(g_pin_screen, 0, 0);
    lv_obj_set_style_radius(g_pin_screen, 0, 0);
    lv_obj_set_style_pad_all(g_pin_screen, 0, 0);
    lv_obj_set_style_pad_row(g_pin_screen, 0, 0);
    lv_obj_set_style_pad_column(g_pin_screen, 0, 0);
    lv_obj_clear_flag(g_pin_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Create blue header bar
    lv_obj_t *header_bar = lv_obj_create(g_pin_screen);
    lv_obj_set_size(header_bar, g_current_width, PIN_HEADER_HEIGHT);
    lv_obj_align(header_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_hex(PIN_TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(header_bar, 0, 0);
    lv_obj_set_style_radius(header_bar, 0, 0);
    lv_obj_set_style_pad_all(header_bar, 0, 0);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Hamburger button (left side) - rotation
    lv_obj_t *hamburger_btn = lv_btn_create(header_bar);
    lv_obj_set_size(hamburger_btn, 40, 40);
    lv_obj_align(hamburger_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(hamburger_btn, lv_color_hex(PIN_TITLE_BACKGROUND_COLOR), 0);
    lv_obj_set_style_border_width(hamburger_btn, 0, 0);
    lv_obj_set_style_shadow_width(hamburger_btn, 0, 0);
    lv_obj_set_style_pad_all(hamburger_btn, 0, 0);  // Remove default padding
    lv_obj_add_event_cb(hamburger_btn, hamburger_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hamburger_label = lv_label_create(hamburger_btn);
    lv_label_set_text(hamburger_label, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(hamburger_label, lv_color_hex(PIN_TITLE_FOREGROUND_COLOR), 0);
    lv_obj_center(hamburger_label);
    lv_obj_clear_flag(hamburger_label, LV_OBJ_FLAG_CLICKABLE);  // Already had this - good

    // "PIN:" label in header
    lv_obj_t *pin_label = lv_label_create(header_bar);
    lv_label_set_text(pin_label, "PIN:");
    lv_obj_set_style_text_color(pin_label, lv_color_hex(PIN_TITLE_FOREGROUND_COLOR), 0);
    lv_obj_set_style_text_font(pin_label, &lv_font_montserrat_16, 0);
    lv_obj_align(pin_label, LV_ALIGN_LEFT_MID, 45, 0);

    // PIN display (asterisks) - left aligned after "PIN:"
    g_pin_display_label = lv_label_create(header_bar);
    lv_label_set_text(g_pin_display_label, "");
    lv_obj_set_style_text_color(g_pin_display_label, lv_color_hex(PIN_DISPLAY_COLOR), 0);
    lv_obj_set_style_text_font(g_pin_display_label, &lv_font_montserrat_20, 0);
    lv_obj_align(g_pin_display_label, LV_ALIGN_LEFT_MID, 95, 0);

    // Calculate button dimensions based on screen size (responsive)
    // Use ~90% of width for buttons, leave margins
    int available_width = (g_current_width * 90) / 100;
    int available_height = g_current_height - PIN_HEADER_HEIGHT - 20;  // 20px bottom margin
    int button_width = (available_width - (2 * PIN_BUTTON_GAP)) / 3;
    int button_height = (available_height - (3 * PIN_BUTTON_GAP)) / 4;

    // Ensure buttons are reasonable size (not too small)
    if (button_width < 60) button_width = 60;
    if (button_height < 35) button_height = 35;
    int total_width = (button_width * 3) + (PIN_BUTTON_GAP * 2);
    int total_height = (button_height * 4) + (PIN_BUTTON_GAP * 3);
    int start_x = (g_current_width - total_width) / 2;
    int start_y = PIN_HEADER_HEIGHT + ((g_current_height - PIN_HEADER_HEIGHT) - total_height) / 2;

    // Use bigger font in landscape mode for better readability
    const lv_font_t *button_font = g_is_portrait ? &lv_font_montserrat_20 : &lv_font_montserrat_48;
    ESP_LOGI(TAG, "Button sizing: portrait=%d, screen=%dx%d, button=%dx%d", g_is_portrait, g_current_width, g_current_height, button_width, button_height);

    // Create number buttons 1-9 in 3x3 grid
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int digit = (row * 3) + col + 1;
            int x = start_x + (col * (button_width + PIN_BUTTON_GAP));
            int y = start_y + (row * (button_height + PIN_BUTTON_GAP));

            lv_obj_t *btn = lv_btn_create(g_pin_screen);
            lv_obj_set_size(btn, button_width, button_height);
            lv_obj_set_pos(btn, x, y);
            lv_obj_set_style_bg_color(btn, lv_color_hex(PIN_BUTTON_BG_COLOR), 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);  // Remove default padding - make full button clickable
            lv_obj_add_event_cb(btn, number_btn_event_cb, LV_EVENT_PRESSED, (void *)(intptr_t)digit);
            lv_obj_add_event_cb(btn, number_btn_event_cb, LV_EVENT_RELEASED, (void *)(intptr_t)digit);
            lv_obj_add_event_cb(btn, number_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)digit);

            lv_obj_t *label = lv_label_create(btn);
            lv_label_set_text_fmt(label, "%d", digit);
            lv_obj_set_style_text_color(label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0);
            lv_obj_set_style_text_font(label, button_font, 0);
            lv_obj_center(label);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);  // Let touches pass through to button
        }
    }
    // Bottom row: Backspace, 0, Enter
    int bottom_y = start_y + (3 * (button_height + PIN_BUTTON_GAP));

    // Backspace button (red)
    lv_obj_t *backspace_btn = lv_btn_create(g_pin_screen);
    lv_obj_set_size(backspace_btn, button_width, button_height);
    lv_obj_set_pos(backspace_btn, start_x, bottom_y);
    lv_obj_set_style_bg_color(backspace_btn, lv_color_hex(PIN_BUTTON_BACKSPACE_COLOR), 0);
    lv_obj_set_style_radius(backspace_btn, 8, 0);
    lv_obj_set_style_pad_all(backspace_btn, 0, 0);  // Remove default padding
    lv_obj_add_event_cb(backspace_btn, backspace_btn_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(backspace_btn, backspace_btn_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(backspace_btn, backspace_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *backspace_label = lv_label_create(backspace_btn);
    lv_label_set_text(backspace_label, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_color(backspace_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(backspace_label, button_font, 0);
    lv_obj_center(backspace_label);
    lv_obj_clear_flag(backspace_label, LV_OBJ_FLAG_CLICKABLE);  // Let touches pass through

    // 0 button
    lv_obj_t *zero_btn = lv_btn_create(g_pin_screen);
    lv_obj_set_size(zero_btn, button_width, button_height);
    lv_obj_set_pos(zero_btn, start_x + (button_width + PIN_BUTTON_GAP), bottom_y);
    lv_obj_set_style_bg_color(zero_btn, lv_color_hex(PIN_BUTTON_BG_COLOR), 0);
    lv_obj_set_style_radius(zero_btn, 8, 0);
    lv_obj_set_style_pad_all(zero_btn, 0, 0);  // Remove default padding
    lv_obj_add_event_cb(zero_btn, number_btn_event_cb, LV_EVENT_PRESSED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(zero_btn, number_btn_event_cb, LV_EVENT_RELEASED, (void *)(intptr_t)0);
    lv_obj_add_event_cb(zero_btn, number_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    lv_obj_t *zero_label = lv_label_create(zero_btn);
    lv_label_set_text(zero_label, "0");
    lv_obj_set_style_text_color(zero_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0);
    lv_obj_set_style_text_font(zero_label, button_font, 0);
    lv_obj_center(zero_label);
    lv_obj_clear_flag(zero_label, LV_OBJ_FLAG_CLICKABLE);  // Let touches pass through

    // Enter button (starts disabled/gray)
    g_enter_btn = lv_btn_create(g_pin_screen);
    lv_obj_set_size(g_enter_btn, button_width, button_height);
    int enter_x = start_x + (2 * (button_width + PIN_BUTTON_GAP));
    lv_obj_set_pos(g_enter_btn, enter_x, bottom_y);
    ESP_LOGI(TAG, "Enter button bounds: x=%d to %d, y=%d to %d (w=%d, h=%d)", enter_x, enter_x + button_width, bottom_y, bottom_y + button_height, button_width, button_height);
    lv_obj_set_style_bg_color(g_enter_btn, lv_color_hex(PIN_BUTTON_ENTER_DISABLED_COLOR), 0);
    lv_obj_set_style_radius(g_enter_btn, 8, 0);
    lv_obj_set_style_pad_all(g_enter_btn, 0, 0);  // Remove default padding
    lv_obj_add_event_cb(g_enter_btn, enter_btn_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_enter_btn, enter_btn_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_enter_btn, enter_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(g_enter_btn, LV_STATE_DISABLED);
    g_enter_label = lv_label_create(g_enter_btn);
    lv_label_set_text(g_enter_label, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(g_enter_label, lv_color_hex(PIN_BUTTON_TEXT_COLOR), 0);  // White like other buttons
    lv_obj_set_style_text_font(g_enter_label, button_font, 0);
    lv_obj_center(g_enter_label);
    lv_obj_clear_flag(g_enter_label, LV_OBJ_FLAG_CLICKABLE);  // Let touches pass through
    ESP_LOGI(TAG, "PIN entry screen created");
} /**/


/**
 * checking if pin length matches
 */
bool display_pin_is_complete(char *pin_out) {
    if (g_pin_complete && pin_out) {
        strncpy(pin_out, g_pin_buffer, PIN_MAX_LENGTH);
        pin_out[PIN_MAX_LENGTH] = '\0';
        return true;
    }
    return false;
} /**/


