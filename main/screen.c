/*
 * Orientation: by default the picture is the panel's own one turned 180° (the operator prefers the
 * board that way round); "upside down" in Settings shows the panel's own orientation.
 *
 * Turning: the ST7701 panel controller flips the picture itself (horizontal: SDIR's SS bit,
 * vertical: MADCTL's ML bit), so drawing costs nothing extra. The commands go over the panel's
 * 3-wire SPI line, which runs through the IO expander. The BSP deletes that line once the panel
 * is set up, so it is created again here for the few commands and deleted afterwards.
 * Touch coordinates are flipped to match, in a wrapper around the LVGL touch read callback.
 *
 * Touch X correction: the touch chip stretches X towards the side edges. Taps within about 28 px
 * of the left or right edge all read 0 or 479, and in the middle it is off by up to ~11 px
 * (measured with test taps on crosshairs). Squeezing X into 20..459 puts edge taps back on the
 * controls there; in the middle taps then land within ~7 px. Y is accurate and left alone.
 */
#include "bsp/esp-bsp.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_st7701.h"
#include "esp_log.h"
#include "screen.h"
#include "settings.h"

static const char *TAG = "screen";

/* ST7701 registers, as used by Espressif's esp_lcd_st7701 driver */
#define ST7701_CMD_CND2BKxSEL 0xFF /* select a Command2 bank: 77 01 00 00 <bank> */
#define ST7701_CMD_SDIR       0xC7 /* in bank 0 */
#define ST7701_SDIR_SS        0x04 /* source (column) order reversed */
#define LCD_MADCTL            0x36
#define LCD_MADCTL_ML         0x10 /* line order reversed; the BSP sets MADCTL to 0 */

#define TOUCH_X_MARGIN 20 /* px: the touch chip's 0..479 is mapped onto 20..459 */

static lv_indev_read_cb_t s_touch_read; /* esp_lvgl_port's own read callback */
static volatile bool s_panel_flipped;  /* the panel is showing its picture turned 180° */

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_touch_read(indev, data);
    /* On release the point is left at the last (already corrected) position: only correct presses */
    if (data->state != LV_INDEV_STATE_PRESSED) {
        return;
    }
    data->point.x = TOUCH_X_MARGIN + data->point.x * (BSP_LCD_H_RES - 1 - 2 * TOUCH_X_MARGIN) / (BSP_LCD_H_RES - 1);
    if (s_panel_flipped) {
        data->point.x = BSP_LCD_H_RES - 1 - data->point.x;
        data->point.y = BSP_LCD_V_RES - 1 - data->point.y;
    }
}

static esp_err_t send_flip(bool on)
{
    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_EXPANDER,
        .cs_expander_pin = BSP_LCD_IO_SPI_CS,
        .scl_io_type = IO_TYPE_EXPANDER,
        .scl_expander_pin = BSP_LCD_IO_SPI_SCL,
        .sda_io_type = IO_TYPE_EXPANDER,
        .sda_expander_pin = BSP_LCD_IO_SPI_SDA,
        .io_expander = bsp_io_expander_init(), /* the BSP's expander, already set up */
    };
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);
    esp_lcd_panel_io_handle_t io = NULL;
    esp_err_t err = esp_lcd_new_panel_io_3wire_spi(&io_config, &io);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t bank0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
    const uint8_t bank_off[] = {0x77, 0x01, 0x00, 0x00, 0x00};
    const uint8_t sdir = on ? ST7701_SDIR_SS : 0;
    const uint8_t madctl = on ? LCD_MADCTL_ML : 0;
    err = esp_lcd_panel_io_tx_param(io, ST7701_CMD_CND2BKxSEL, bank0, sizeof(bank0));
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(io, ST7701_CMD_SDIR, &sdir, 1);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(io, ST7701_CMD_CND2BKxSEL, bank_off, sizeof(bank_off));
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(io, LCD_MADCTL, &madctl, 1);
    }
    esp_lcd_panel_io_del(io);
    return err;
}

void screen_set_brightness(int percent)
{
    bsp_display_brightness_set(percent);
}

void screen_set_upside_down(bool on)
{
    bool flip = !on;
    esp_err_t err = send_flip(flip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not turn the panel: %s", esp_err_to_name(err));
        return;
    }
    s_panel_flipped = flip;
    ESP_LOGI(TAG, "Picture upside down: %s", on ? "on" : "off");
}

void screen_init(void)
{
    /* The BSP logs every brightness change, which floods the console while the slider moves */
    esp_log_level_set("ESP32-S3-Touch-LCD-4B", ESP_LOG_WARN);

    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch) {
        s_touch_read = lv_indev_get_read_cb(touch);
        lv_indev_set_read_cb(touch, touch_read);
    }
    screen_set_brightness(settings_get_brightness());
    screen_set_upside_down(settings_get_upside_down()); /* the default needs the panel turned too */
}
