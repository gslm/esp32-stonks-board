/*
 * Stock watchlist for the Waveshare ESP32-S3-Touch-LCD-4B.
 *
 * Shows the Investing.com "Ativos" watchlist (B3 stocks) with prices from Yahoo Finance,
 * refreshed every 30 s by default, as an endless auto-rolling wheel. Tap a row for details.
 * The serial console ("stock>" prompt on the USB TO UART port) sets up Wi-Fi and shows status.
 *
 * Tasks:
 *   - LVGL task (esp_lvgl_port, started by the BSP): draws the UI, polls the model every 250 ms
 *   - quotes task: all HTTP requests, writes results into the model
 *   - console task (esp_console REPL): the command line
 * The model (stock_model.c) is the only shared state and has its own mutex.
 */
#include <stdlib.h>
#include <time.h>
#include "bsp/esp-bsp.h"
#include "cli.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "quotes.h"
#include "stock_model.h"
#include "ui.h"
#include "wifi.h"

static const char *TAG = "app";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was reformatted (%s); saved settings are lost", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void log_memory(const char *when)
{
    ESP_LOGI(TAG, "Free memory %s: internal %u (largest block %u), PSRAM %u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void app_main(void)
{
    init_nvs();
    /* Brasília time, UTC-3, no daylight saving since 2019 */
    setenv("TZ", "<-03>3", 1);
    tzset();
    model_init();

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    bsp_display_brightness_set(100);
    bsp_display_lock(0);
    ui_init();
    bsp_display_unlock();
    log_memory("after display init");

    wifi_start();
    quotes_start();
    cli_start();
    log_memory("after network init");
}
