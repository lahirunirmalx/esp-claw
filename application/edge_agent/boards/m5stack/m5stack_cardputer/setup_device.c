/*
 * M5Stack Cardputer board-specific factories.
 * Provides the ST7789 panel factory used by the generic display_lcd device.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "M5_CARDPUTER_SETUP";

/* Cardputer's 240x135 panel sits at (40, 53) in the ST7789's 240x320 buffer. */
#define M5_CARDPUTER_LCD_OFFSET_X  40
#define M5_CARDPUTER_LCD_OFFSET_Y  53

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));

    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_set_gap(*ret_panel,
                                M5_CARDPUTER_LCD_OFFSET_X,
                                M5_CARDPUTER_LCD_OFFSET_Y);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_panel_set_gap failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}
