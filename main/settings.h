/*
 * Persistent settings in NVS: Wi-Fi credentials, the refresh interval and the roll speed.
 * Values saved with the console commands take precedence over the menuconfig defaults.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_SSID_LEN     33 /* 32 chars + NUL, the 802.11 limit */
#define SETTINGS_PASSWORD_LEN 65 /* 64 chars + NUL */
#define SETTINGS_REFRESH_MIN_S 10
#define SETTINGS_REFRESH_MAX_S 3600

/* Returns false when no SSID is configured anywhere. */
bool settings_get_wifi(char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN]);
esp_err_t settings_set_wifi(const char *ssid, const char *password);

/* Auto-roll speed in tenths: 0 = stopped .. 10 = 1.0x (one row per second). Cached in RAM,
   so the UI can read it every frame. `save` writes it to NVS too (do that once, not while dragging). */
#define SETTINGS_ROLL_SPEED_MAX     10
#define SETTINGS_ROLL_SPEED_DEFAULT 3
int settings_get_roll_speed(void);
esp_err_t settings_set_roll_speed(int tenths, bool save); /* ESP_ERR_INVALID_ARG outside 0..10 */

int settings_get_refresh_s(void);
esp_err_t settings_set_refresh_s(int seconds); /* ESP_ERR_INVALID_ARG outside 10..3600 */
