/*
 * Wi-Fi station: connects with the saved credentials, reconnects on its own, and starts SNTP
 * so the clock on screen shows local (Brasília) time.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

void wifi_start(void);
/* Saves new credentials and reconnects with them. An empty password means an open network. */
esp_err_t wifi_set_credentials(const char *ssid, const char *password);
bool wifi_is_connected(void);
/* Signal strength in dBm, 0 when not connected. */
int wifi_rssi(void);
/* wifi_err_reason_t of the last disconnect, 0 if none since the last config change.
   15 (4-way handshake timeout) or 202 (auth fail) usually mean a wrong password; 201 = SSID not found. */
int wifi_last_disconnect_reason(void);
/* "192.168.1.20", or "" when not connected */
void wifi_ip_string(char *buf, size_t len);
