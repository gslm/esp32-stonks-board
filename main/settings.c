#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "sdkconfig.h"
#include "settings.h"

#define NVS_NAMESPACE "settings"

static esp_err_t get_str(const char *key, char *out, size_t len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, key, out, &len);
        nvs_close(nvs);
    }
    return err;
}

/* Profile defaults: the local file holds the real passwords; a fresh clone falls back to the empty template */
#if __has_include("wifi_profiles_local.h")
#include "wifi_profiles_local.h"
#else
#include "wifi_profiles_local.example.h"
#endif

static const struct {
    const char *name;
    const char *ssid;
    const char *password;
} DEFAULT_PROFILES[SETTINGS_WIFI_PROFILES] = {
    {"Work", WIFI_WORK_SSID, WIFI_WORK_PASSWORD},
    {"Home2", WIFI_HOME2_SSID, WIFI_HOME2_PASSWORD},
    {"Home3", WIFI_HOME3_SSID, WIFI_HOME3_PASSWORD},
    {"Hotspot - Test", WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASSWORD},
};

static int clamp_profile(int profile)
{
    return (profile >= 0 && profile < SETTINGS_WIFI_PROFILES) ? profile : 0;
}

const char *settings_wifi_profile_name(int profile)
{
    return DEFAULT_PROFILES[clamp_profile(profile)].name;
}

void settings_get_wifi_profile(int profile, char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN])
{
    profile = clamp_profile(profile);
    char key[16];
    snprintf(key, sizeof(key), "wifi%d_ssid", profile);
    if (get_str(key, ssid, SETTINGS_SSID_LEN) != ESP_OK) {
        /* Never edited on the device: use the defaults */
        strlcpy(ssid, DEFAULT_PROFILES[profile].ssid, SETTINGS_SSID_LEN);
        strlcpy(password, DEFAULT_PROFILES[profile].password, SETTINGS_PASSWORD_LEN);
        return;
    }
    snprintf(key, sizeof(key), "wifi%d_pass", profile);
    if (get_str(key, password, SETTINGS_PASSWORD_LEN) != ESP_OK) {
        password[0] = '\0';
    }
}

esp_err_t settings_set_wifi_profile(int profile, const char *ssid, const char *password)
{
    if (profile < 0 || profile >= SETTINGS_WIFI_PROFILES || strlen(ssid) >= SETTINGS_SSID_LEN ||
        strlen(password) >= SETTINGS_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    char key[16];
    snprintf(key, sizeof(key), "wifi%d_ssid", profile);
    err = nvs_set_str(nvs, key, ssid);
    if (err == ESP_OK) {
        snprintf(key, sizeof(key), "wifi%d_pass", profile);
        err = nvs_set_str(nvs, key, password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

int settings_get_wifi_active(void)
{
    uint8_t value = 0;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "wifi_active", &value);
        nvs_close(nvs);
    }
    return clamp_profile(value);
}

esp_err_t settings_set_wifi_active(int profile)
{
    if (profile < 0 || profile >= SETTINGS_WIFI_PROFILES) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, "wifi_active", (uint8_t)profile);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool settings_get_wifi(char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN])
{
    settings_get_wifi_profile(settings_get_wifi_active(), ssid, password);
    return ssid[0] != '\0';
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    return settings_set_wifi_profile(settings_get_wifi_active(), ssid, password);
}

static volatile int s_roll_speed = -1; /* -1 = not loaded from NVS yet */

int settings_get_roll_speed(void)
{
    if (s_roll_speed < 0) {
        uint8_t value = SETTINGS_ROLL_SPEED_DEFAULT;
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
            nvs_get_u8(nvs, "roll_speed", &value);
            nvs_close(nvs);
        }
        s_roll_speed = value <= SETTINGS_ROLL_SPEED_MAX ? value : SETTINGS_ROLL_SPEED_DEFAULT;
    }
    return s_roll_speed;
}

esp_err_t settings_set_roll_speed(int tenths, bool save)
{
    if (tenths < 0 || tenths > SETTINGS_ROLL_SPEED_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    s_roll_speed = tenths;
    if (!save) {
        return ESP_OK;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, "roll_speed", (uint8_t)tenths);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static volatile int8_t s_autoscroll[SETTINGS_PAGES] = {-1, -1}; /* -1 = not loaded from NVS yet */

bool settings_get_autoscroll(int page)
{
    if (page < 0 || page >= SETTINGS_PAGES) {
        return false;
    }
    if (s_autoscroll[page] < 0) {
        uint8_t value = 1;
        char key[16];
        snprintf(key, sizeof(key), "autoscroll%d", page);
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
            nvs_get_u8(nvs, key, &value);
            nvs_close(nvs);
        }
        s_autoscroll[page] = value != 0;
    }
    return s_autoscroll[page] != 0;
}

esp_err_t settings_set_autoscroll(int page, bool on)
{
    if (page < 0 || page >= SETTINGS_PAGES) {
        return ESP_ERR_INVALID_ARG;
    }
    s_autoscroll[page] = on;
    char key[16];
    snprintf(key, sizeof(key), "autoscroll%d", page);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, key, on ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static esp_err_t save_u8(const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static uint8_t load_u8(const char *key, uint8_t default_value)
{
    uint8_t value = default_value;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, key, &value);
        nvs_close(nvs);
    }
    return value;
}

int settings_get_brightness(void)
{
    int value = load_u8("brightness", 100);
    return value >= SETTINGS_BRIGHTNESS_MIN && value <= 100 ? value : 100;
}

esp_err_t settings_set_brightness(int percent)
{
    if (percent < SETTINGS_BRIGHTNESS_MIN || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    return save_u8("brightness", (uint8_t)percent);
}

/* Stored as "orientation": the older "upside_down" key had the opposite meaning (the default
   orientation changed), so it is ignored */
bool settings_get_upside_down(void)
{
    return load_u8("orientation", 0) != 0;
}

esp_err_t settings_set_upside_down(bool on)
{
    return save_u8("orientation", on ? 1 : 0);
}

int settings_get_refresh_s(void)
{
    int32_t value = CONFIG_STOCK_REFRESH_INTERVAL_S;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_i32(nvs, "refresh_s", &value);
        nvs_close(nvs);
    }
    if (value < SETTINGS_REFRESH_MIN_S || value > SETTINGS_REFRESH_MAX_S) {
        value = CONFIG_STOCK_REFRESH_INTERVAL_S;
    }
    return (int)value;
}

esp_err_t settings_set_refresh_s(int seconds)
{
    if (seconds < SETTINGS_REFRESH_MIN_S || seconds > SETTINGS_REFRESH_MAX_S) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(nvs, "refresh_s", seconds);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}
