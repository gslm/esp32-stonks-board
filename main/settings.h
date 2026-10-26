/*
 * Persistent settings in NVS: Wi-Fi profiles, the refresh interval, the roll speed, which
 * main-screen pages auto-scroll, and the screen's brightness and orientation.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_SSID_LEN     33 /* 32 chars + NUL, the 802.11 limit */
#define SETTINGS_PASSWORD_LEN 65 /* 64 chars + NUL */
#define SETTINGS_REFRESH_MIN_S 10
#define SETTINGS_REFRESH_MAX_S 3600

/*
 * Wi-Fi profiles: Work, Home2, Home3, Hotspot - Test. Each starts from the defaults in wifi_profiles_local.h
 * (git-ignored) until it is edited on the device, which saves it in NVS. One profile is active;
 * Work when nothing is saved yet.
 */
#define SETTINGS_WIFI_PROFILES 4
const char *settings_wifi_profile_name(int profile);
void settings_get_wifi_profile(int profile, char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN]);
esp_err_t settings_set_wifi_profile(int profile, const char *ssid, const char *password);
int settings_get_wifi_active(void);
esp_err_t settings_set_wifi_active(int profile);

/* Credentials of the active profile. Returns false when its SSID is empty. */
bool settings_get_wifi(char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN]);
/* Saves new credentials into the active profile (the "wifi" console command). */
esp_err_t settings_set_wifi(const char *ssid, const char *password);

/* Auto-roll speed in tenths: 0 = stopped .. 10 = 1.0x (one row per second). Cached in RAM,
   so the UI can read it every frame. `save` writes it to NVS too (do that once, not while dragging). */
#define SETTINGS_ROLL_SPEED_MAX     10
#define SETTINGS_ROLL_SPEED_DEFAULT 3
int settings_get_roll_speed(void);
esp_err_t settings_set_roll_speed(int tenths, bool save); /* ESP_ERR_INVALID_ARG outside 0..10 */

/* Auto-roll on or off for each main-screen page: 0 = Stocks, 1 = Market. On by default. Cached in RAM. */
#define SETTINGS_PAGES 2
bool settings_get_autoscroll(int page);
esp_err_t settings_set_autoscroll(int page, bool on); /* ESP_ERR_INVALID_ARG for an unknown page */

/* Backlight in %, 10..100 (never fully dark), default 100. Applied by screen.c, saved here. */
#define SETTINGS_BRIGHTNESS_MIN 10
int settings_get_brightness(void);
esp_err_t settings_set_brightness(int percent); /* ESP_ERR_INVALID_ARG outside 10..100 */

/* "Upside down" in Settings > Screen, default off. Off is the panel turned 180° (see screen.c). */
bool settings_get_upside_down(void);
esp_err_t settings_set_upside_down(bool on);

int settings_get_refresh_s(void);
esp_err_t settings_set_refresh_s(int seconds); /* ESP_ERR_INVALID_ARG outside 10..3600 */
