/*
 * Serial command line ("stock>" prompt) built on esp_console. It runs on the primary console,
 * UART0, which is the board's "USB TO UART" USB-C port. Type "help" for the command list.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cli.h"
#include "quotes.h"
#include "settings.h"
#include "stock_model.h"
#include "wifi.h"

static const char *TAG = "cli";

EXT_RAM_BSS_ATTR static stock_t s_list[STOCK_MAX];

static void print_value(double v, bool valid, const char *suffix)
{
    if (valid && !isnan(v)) {
        printf("%9.2f%s", v, suffix);
    } else {
        printf("%9s%s", "--", suffix[0] == '%' ? " " : suffix);
    }
}

static int cmd_list(int argc, char **argv)
{
    int n = model_snapshot(s_list, STOCK_MAX);
    printf("%-8s %9s %9s %9s  %s\n", "Symbol", "Last", "Chg", "Chg %", "Name");
    for (int i = 0; i < n; i++) {
        const stock_t *s = &s_list[i];
        char sym[STOCK_SYMBOL_LEN];
        printf("%-8s ", stock_display_symbol(s->symbol, sym, sizeof(sym)));
        print_value(s->last, s->has_quote, " ");
        print_value(s->change, s->has_quote, " ");
        print_value(s->change_pct, s->has_quote, "%");
        printf("  %s%s\n", s->name, s->not_found ? "  [no data on Yahoo]" : "");
    }
    printf("%d stocks\n", n);
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc == 1) {
        char ssid[SETTINGS_SSID_LEN];
        char password[SETTINGS_PASSWORD_LEN];
        char ip[16];
        bool configured = settings_get_wifi(ssid, password);
        wifi_ip_string(ip, sizeof(ip));
        printf("Profile: %s, SSID: %s\n", settings_wifi_profile_name(settings_get_wifi_active()),
               configured ? ssid : "(not set)");
        if (wifi_is_connected()) {
            printf("Connected, IP %s, signal %d dBm\n", ip, wifi_rssi());
        } else {
            int reason = wifi_last_disconnect_reason();
            printf("Not connected");
            if (reason) {
                printf(" (last disconnect reason %d%s)", reason,
                       reason == 201 ? ": network not found" :
                       (reason == 15 || reason == 202) ? ": wrong password?" : "");
            }
            printf("\n");
        }
        printf("To change this profile: wifi \"<ssid>\" \"<password>\"  (or Settings > Wi-Fi on screen)\n");
        return 0;
    }
    if (argc > 3) {
        printf("Usage: wifi \"<ssid>\" \"<password>\"   (quote names with spaces)\n");
        return 1;
    }
    esp_err_t err = wifi_set_credentials(argv[1], argc == 3 ? argv[2] : "");
    if (err != ESP_OK) {
        printf("Could not apply: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Saved. Connecting to \"%s\"...\n", argv[1]);
    return 0;
}

static int cmd_interval(int argc, char **argv)
{
    if (argc == 1) {
        printf("Refresh interval: %d s\n", settings_get_refresh_s());
        return 0;
    }
    int seconds = atoi(argv[1]);
    if (settings_set_refresh_s(seconds) != ESP_OK) {
        printf("Interval must be %d..%d seconds\n", SETTINGS_REFRESH_MIN_S, SETTINGS_REFRESH_MAX_S);
        return 1;
    }
    quotes_refresh_now();
    printf("Refresh interval set to %d s\n", seconds);
    return 0;
}

static int cmd_speed(int argc, char **argv)
{
    if (argc == 2) {
        char *end = NULL;
        float x = strtof(argv[1], &end);
        int tenths = (int)lroundf(x * 10.0f);
        if (end == argv[1] || settings_set_roll_speed(tenths, true) != ESP_OK) {
            printf("Use 0 (stopped) to 1.0, e.g. speed 0.3\n");
            return 1;
        }
    }
    int tenths = settings_get_roll_speed();
    printf("Auto-roll speed: %d.%dx%s\n", tenths / 10, tenths % 10, tenths ? "" : " (stopped)");
    return 0;
}

static int cmd_refresh(int argc, char **argv)
{
    quotes_refresh_now();
    printf("Refreshing\n");
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    app_status_t st;
    model_get_status(&st);
    static const char *const NET[] = {"Wi-Fi not configured", "connecting", "online"};
    char ip[16];
    wifi_ip_string(ip, sizeof(ip));
    printf("Network:   %s %s\n", NET[st.net], ip);
    if (st.last_update && st.last_update < 1704067200 /* 2024-01-01 */) {
        printf("Updated:   yes (clock not synced yet)\n");
    } else if (st.last_update) {
        struct tm tm;
        localtime_r(&st.last_update, &tm);
        printf("Updated:   %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        printf("Updated:   never\n");
    }
    printf("Error:     %s\n", st.error[0] ? st.error : "none");
    printf("Interval:  %d s\n", settings_get_refresh_s());
    printf("Stocks:    %d\n", model_count());
    printf("Heap:      internal %u free (largest block %u), PSRAM %u free\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("Uptime:    %lld s\n", esp_timer_get_time() / 1000000);
    return 0;
}

void cli_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "stock>";
    repl_config.max_cmdline_length = 160;
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    const esp_console_cmd_t cmds[] = {
        {.command = "list", .help = "Show the watchlist with the latest quotes", .func = cmd_list},
        {.command = "wifi", .help = "Show Wi-Fi status, or set the active profile: wifi \"<ssid>\" \"<password>\"", .hint = "[<ssid> [<password>]]", .func = cmd_wifi},
        {.command = "interval", .help = "Show or set the refresh interval in seconds (10..3600)", .hint = "[<seconds>]", .func = cmd_interval},
        {.command = "speed", .help = "Show or set the auto-roll speed: 0 (stopped) to 1.0 (one row per second), default 0.3", .hint = "[<x>]", .func = cmd_speed},
        {.command = "refresh", .help = "Fetch all quotes now", .func = cmd_refresh},
        {.command = "status", .help = "Network, last update, errors, memory", .func = cmd_status},
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console ready on the USB TO UART port. Type 'help'.");
}
