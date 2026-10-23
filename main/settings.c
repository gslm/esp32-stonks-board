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

bool settings_get_wifi(char ssid[SETTINGS_SSID_LEN], char password[SETTINGS_PASSWORD_LEN])
{
    if (get_str("wifi_ssid", ssid, SETTINGS_SSID_LEN) != ESP_OK || ssid[0] == '\0') {
        strlcpy(ssid, CONFIG_STOCK_WIFI_SSID, SETTINGS_SSID_LEN);
        strlcpy(password, CONFIG_STOCK_WIFI_PASSWORD, SETTINGS_PASSWORD_LEN);
    } else if (get_str("wifi_pass", password, SETTINGS_PASSWORD_LEN) != ESP_OK) {
        password[0] = '\0';
    }
    return ssid[0] != '\0';
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    if (strlen(ssid) >= SETTINGS_SSID_LEN || strlen(password) >= SETTINGS_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_pass", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
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
