#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "quotes.h"
#include "settings.h"
#include "stock_model.h"
#include "wifi.h"

static const char *TAG = "wifi";

#define CONNECTED_BIT    BIT0
#define RECONNECT_DELAY_US (5 * 1000 * 1000)

static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_reconnect_timer;
static bool s_configured;
static int s_last_reason;

/* Scan of nearby networks (Settings > Wi-Fi > Scan) */
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_network_t s_scan[WIFI_SCAN_MAX];
static int s_scan_count;
static uint32_t s_scan_version;
static volatile bool s_scan_running;

static void reconnect_cb(void *arg)
{
    if (s_configured && !s_scan_running && !(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        esp_wifi_connect();
    }
}

static int compare_rssi(const void *a, const void *b)
{
    return ((const wifi_network_t *)b)->rssi - ((const wifi_network_t *)a)->rssi;
}

/* Runs in the event loop task when the radio has finished scanning */
static void on_scan_done(void)
{
    static wifi_ap_record_t records[WIFI_SCAN_MAX];
    static wifi_network_t found[WIFI_SCAN_MAX];
    uint16_t n = WIFI_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
        n = 0;
    }
    for (int i = 0; i < n; i++) {
        strlcpy(found[i].ssid, (const char *)records[i].ssid, sizeof(found[i].ssid));
        found[i].rssi = records[i].rssi;
        found[i].channel = records[i].primary;
        found[i].open = records[i].authmode == WIFI_AUTH_OPEN;
    }
    qsort(found, n, sizeof(found[0]), compare_rssi);
    portENTER_CRITICAL(&s_scan_lock);
    memcpy(s_scan, found, n * sizeof(found[0]));
    s_scan_count = n;
    s_scan_version++;
    portEXIT_CRITICAL(&s_scan_lock);
    s_scan_running = false;
    ESP_LOGI(TAG, "Scan found %d networks", n);
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "  %4d dBm  ch %2d  %s", found[i].rssi, found[i].channel, found[i].ssid[0] ? found[i].ssid : "(hidden)");
    }
    /* Resume connecting, if the scan interrupted it */
    if (s_configured && !(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        esp_wifi_connect();
    }
}

/* Short-lived task: the radio refuses to scan (ESP_ERR_WIFI_STATE) while a connection attempt is
   in progress, so stop that attempt and retry for a moment */
static void scan_task(void *arg)
{
    esp_timer_stop(s_reconnect_timer);
    if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        esp_wifi_disconnect();
    }
    wifi_scan_config_t cfg = {0}; /* all channels, active scan */
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 30; i++) {
        err = esp_wifi_scan_start(&cfg, false);
        if (err != ESP_ERR_WIFI_STATE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan could not start: %s", esp_err_to_name(err));
        s_scan_running = false;
        if (s_configured && !(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
            esp_wifi_connect();
        }
    }
    vTaskDelete(NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        if (s_configured) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        s_last_reason = ev->reason;
        /* ASSOC_LEAVE is our own esp_wifi_disconnect() while switching networks: no retry needed */
        if (s_configured && ev->reason != WIFI_REASON_ASSOC_LEAVE) {
            ESP_LOGW(TAG, "Disconnected (reason %d), retrying in %d s", ev->reason, RECONNECT_DELAY_US / 1000000);
            model_set_net_state(NET_CONNECTING);
            esp_timer_stop(s_reconnect_timer);
            esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_US);
        }
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        on_scan_done();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "Connected, IP " IPSTR, IP2STR(&ev->ip_info.ip));
    xEventGroupSetBits(s_events, CONNECTED_BIT);
    model_set_net_state(NET_ONLINE);
    quotes_refresh_now();
}

static esp_err_t apply_config(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    /* Accept any security level the AP offers, including open networks when no password is set */
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK) {
        s_configured = ssid[0] != '\0';
    }
    return err;
}

void wifi_start(void)
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    /* Credentials live in our own NVS namespace; keep the driver from writing flash on every connect */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));

    const esp_timer_create_args_t timer_args = {.callback = reconnect_cb, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    char ssid[SETTINGS_SSID_LEN];
    char password[SETTINGS_PASSWORD_LEN];
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    if (settings_get_wifi(ssid, password)) {
        ESP_LOGI(TAG, "Connecting to \"%s\" (profile %s)", ssid, settings_wifi_profile_name(settings_get_wifi_active()));
        ESP_ERROR_CHECK(apply_config(ssid, password));
        model_set_net_state(NET_CONNECTING);
    } else {
        ESP_LOGW(TAG, "Wi-Fi profile \"%s\" has no SSID. Set it in Settings (gear) > Wi-Fi",
                 settings_wifi_profile_name(settings_get_wifi_active()));
        model_set_net_state(NET_NOT_CONFIGURED);
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Clock for the "Updated hh:mm:ss" label; the time zone is set in app_main */
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
}

/* Drops the current connection and connects with new credentials (an empty SSID just stops) */
static esp_err_t connect_to(const char *ssid, const char *password)
{
    esp_timer_stop(s_reconnect_timer);
    esp_wifi_disconnect();
    /* The driver refuses a new config (ESP_ERR_WIFI_STATE) while a connection attempt is winding down */
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 20; i++) {
        err = apply_config(ssid, password);
        if (err != ESP_ERR_WIFI_STATE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (err != ESP_OK) {
        return err;
    }
    s_last_reason = 0;
    if (ssid[0] == '\0') {
        model_set_net_state(NET_NOT_CONFIGURED);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Connecting to \"%s\"", ssid);
    model_set_net_state(NET_CONNECTING);
    return esp_wifi_connect();
}

esp_err_t wifi_set_credentials(const char *ssid, const char *password)
{
    esp_err_t err = settings_set_wifi(ssid, password);
    return err == ESP_OK ? connect_to(ssid, password) : err;
}

esp_err_t wifi_use_profile(int profile)
{
    esp_err_t err = settings_set_wifi_active(profile);
    if (err != ESP_OK) {
        return err;
    }
    char ssid[SETTINGS_SSID_LEN];
    char password[SETTINGS_PASSWORD_LEN];
    settings_get_wifi_profile(profile, ssid, password);
    return connect_to(ssid, password);
}

bool wifi_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & CONNECTED_BIT);
}

int wifi_rssi(void)
{
    int rssi = 0;
    if (!wifi_is_connected() || esp_wifi_sta_get_rssi(&rssi) != ESP_OK) {
        return 0;
    }
    return rssi;
}

esp_err_t wifi_scan_start(void)
{
    if (s_scan_running) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_running = true;
    if (xTaskCreate(scan_task, "wifi_scan", 3072, NULL, 4, NULL) != pdPASS) {
        s_scan_running = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool wifi_scan_running(void)
{
    return s_scan_running;
}

int wifi_scan_results(wifi_network_t *out, int max, uint32_t *version)
{
    portENTER_CRITICAL(&s_scan_lock);
    int n = s_scan_count < max ? s_scan_count : max;
    memcpy(out, s_scan, n * sizeof(out[0]));
    if (version) {
        *version = s_scan_version;
    }
    portEXIT_CRITICAL(&s_scan_lock);
    return n;
}

int wifi_last_disconnect_reason(void)
{
    return s_last_reason;
}

void wifi_ip_string(char *buf, size_t len)
{
    esp_netif_ip_info_t ip;
    if (wifi_is_connected() && esp_netif_get_ip_info(s_netif, &ip) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    } else if (len > 0) {
        buf[0] = '\0';
    }
}
