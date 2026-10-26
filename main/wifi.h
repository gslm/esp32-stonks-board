/*
 * Wi-Fi station: connects with the saved credentials, reconnects on its own, and starts SNTP
 * so the clock on screen shows local (Brasília) time.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

void wifi_start(void);
/* Saves new credentials into the active profile and reconnects. Empty password = open network. */
esp_err_t wifi_set_credentials(const char *ssid, const char *password);
/* Makes a profile (0 Work, 1 Home2, 2 Home3, 3 Hotspot - Test) the active one, saves that, and connects with it. */
esp_err_t wifi_use_profile(int profile);
bool wifi_is_connected(void);
/* Signal strength in dBm, 0 when not connected. */
int wifi_rssi(void);
/* Scan of nearby networks, for Settings > Wi-Fi. It pauses reconnect attempts while it runs. */
#define WIFI_SCAN_MAX 16
typedef struct {
    char ssid[33];
    int rssi;    /* dBm, higher is stronger (-50 good, -80 weak) */
    int channel;
    bool open;   /* no password */
} wifi_network_t;
esp_err_t wifi_scan_start(void); /* ESP_ERR_INVALID_STATE while a scan is running */
bool wifi_scan_running(void);
/* Copies the last scan's networks, strongest first. `version` changes with each new scan. */
int wifi_scan_results(wifi_network_t *out, int max, uint32_t *version);

/* wifi_err_reason_t of the last disconnect, 0 if none since the last config change.
   15 (4-way handshake timeout) or 202 (auth fail) usually mean a wrong password; 201 = SSID not found. */
int wifi_last_disconnect_reason(void);
/* "192.168.1.20", or "" when not connected */
void wifi_ip_string(char *buf, size_t len);
