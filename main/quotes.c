#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "quotes.h"
#include "settings.h"
#include "wifi.h"

static const char *TAG = "quotes";

#define BASE_URL      "https://query1.finance.yahoo.com"
#define TV_SCAN_URL   "https://scanner.tradingview.com/futures/scan" /* DI futures, 15 min delayed */
/* Yahoo answers HTTP 429 to some generic clients (e.g. curl's default); a browser-like agent works */
#define USER_AGENT    "Mozilla/5.0 (compatible; esp32-stock-watchlist/1.0)"
#define RESPONSE_MAX  (48 * 1024)
#define SPARK_BATCH   20 /* symbols per spark request */
#define URL_MAX       (64 + SPARK_BATCH * (STOCK_SYMBOL_LEN + 4))
#define BACKOFF_S     60 /* wait after Yahoo says "too many requests" */

#define NOTIFY_REFRESH BIT0
#define NOTIFY_DETAIL  BIT1

typedef struct {
    char *data;
    size_t len;
    bool overflow;
} response_t;

/* Parsed "chart" response for one symbol */
typedef struct {
    char name[STOCK_NAME_LEN];
    double price;
    double prev_close;
    time_t time;
    double open;
    double high;
    double low;
    double volume;
    int series_len;
    float series[STOCK_SERIES_MAX];
} chart_data_t;

static TaskHandle_t s_task;
static esp_http_client_handle_t s_client;
static response_t s_resp;
static chart_data_t s_chart;
EXT_RAM_BSS_ATTR static stock_t s_list[STOCK_MAX]; /* snapshot used by the fetch task only, in PSRAM */
EXT_RAM_BSS_ATTR static stock_t s_one;             /* one stock, looked up by the fetch task */

/* Detail request from the UI, guarded by a spinlock (only a short copy happens inside) */
static portMUX_TYPE s_req_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_detail_request[STOCK_SYMBOL_LEN];

/* ---------------------------------------------------------------------------------------------- */
/* HTTP                                                                                            */
/* ---------------------------------------------------------------------------------------------- */

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    response_t *r = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (r->len + evt->data_len > RESPONSE_MAX) {
            r->overflow = true;
        } else {
            memcpy(r->data + r->len, evt->data, evt->data_len);
            r->len += evt->data_len;
            r->data[r->len] = '\0';
        }
    }
    return ESP_OK;
}

/*
 * GET, or POST of a JSON body when post_json is not NULL, with a kept-alive connection (it is
 * reopened when the host changes). Returns the HTTP status, or -1 when the request failed.
 */
static int http_request(const char *url, const char *post_json)
{
    if (s_client == NULL) {
        const esp_http_client_config_t cfg = {
            .url = url,
            .user_agent = USER_AGENT,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000,
            .keep_alive_enable = true,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .event_handler = on_http_event,
            .user_data = &s_resp,
        };
        s_client = esp_http_client_init(&cfg);
        if (s_client == NULL) {
            return -1;
        }
    } else {
        esp_http_client_set_url(s_client, url);
    }
    if (post_json) {
        esp_http_client_set_method(s_client, HTTP_METHOD_POST);
        esp_http_client_set_header(s_client, "Content-Type", "application/json");
        esp_http_client_set_post_field(s_client, post_json, (int)strlen(post_json));
    } else {
        esp_http_client_set_method(s_client, HTTP_METHOD_GET);
        esp_http_client_delete_header(s_client, "Content-Type");
        esp_http_client_set_post_field(s_client, NULL, 0);
    }

    s_resp.len = 0;
    s_resp.overflow = false;
    s_resp.data[0] = '\0';
    esp_err_t err = esp_http_client_perform(s_client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(s_client); /* start over with a fresh connection next time */
        s_client = NULL;
        return -1;
    }
    if (s_resp.overflow) {
        ESP_LOGW(TAG, "Response larger than %d bytes, ignored", RESPONSE_MAX);
        return -1;
    }
    return esp_http_client_get_status_code(s_client);
}

/* Yahoo symbols can contain '^' (indices) and '=' (currencies), which must be escaped in a URL */
static void append_symbol(char *url, size_t cap, const char *symbol)
{
    size_t len = strlen(url);
    for (const char *c = symbol; *c && len + 4 < cap; c++) {
        if (*c == '^' || *c == '=') {
            len += snprintf(url + len, cap - len, "%%%02X", (unsigned char)*c);
        } else {
            url[len++] = *c;
            url[len] = '\0';
        }
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* JSON parsing                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

static void *psram_malloc(size_t size)
{
    /* Keep cJSON's many small allocations out of internal RAM, which Wi-Fi and the display need */
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static double number_or_nan(const cJSON *item)
{
    return cJSON_IsNumber(item) ? item->valuedouble : NAN;
}

/* Last non-null number of an array such as "close": [61.2, null, 61.49] */
static double last_number(const cJSON *array)
{
    for (int i = cJSON_GetArraySize(array) - 1; i >= 0; i--) {
        const cJSON *item = cJSON_GetArrayItem(array, i);
        if (cJSON_IsNumber(item)) {
            return item->valuedouble;
        }
    }
    return NAN;
}

/*
 * spark: {"VALE3.SA": {"close": [61.49], "chartPreviousClose": 64.03, "timestamp": [1792699532], ...}, ...}
 * Symbols Yahoo has no data for are simply missing. Returns the number of quotes stored, -1 on bad JSON.
 */
static int parse_spark(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return -1;
    }
    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        double last = last_number(cJSON_GetObjectItem(item, "close"));
        double prev = number_or_nan(cJSON_GetObjectItem(item, "chartPreviousClose"));
        if (isnan(prev)) {
            prev = number_or_nan(cJSON_GetObjectItem(item, "previousClose"));
        }
        double t = last_number(cJSON_GetObjectItem(item, "timestamp"));
        if (item->string && !isnan(last) && !isnan(prev)) {
            model_set_quote(item->string, last, prev, isnan(t) ? 0 : (time_t)t);
            count++;
        }
    }
    cJSON_Delete(root);
    return count;
}

/*
 * chart: {"chart": {"result": [{"meta": {...}, "timestamp": [...],
 *         "indicators": {"quote": [{"open": [...], "high": [...], "low": [...], "close": [...], "volume": [...]}]}}]}}
 */
static bool parse_chart(const char *json, chart_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    const cJSON *result = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "chart"), "result"), 0);
    const cJSON *meta = cJSON_GetObjectItem(result, "meta");
    if (meta == NULL) {
        cJSON_Delete(root);
        return false;
    }

    memset(out, 0, sizeof(*out));
    const cJSON *name = cJSON_GetObjectItem(meta, "longName");
    if (!cJSON_IsString(name)) {
        name = cJSON_GetObjectItem(meta, "shortName");
    }
    if (cJSON_IsString(name)) {
        strlcpy(out->name, name->valuestring, sizeof(out->name));
    }
    out->price = number_or_nan(cJSON_GetObjectItem(meta, "regularMarketPrice"));
    out->prev_close = number_or_nan(cJSON_GetObjectItem(meta, "chartPreviousClose"));
    if (isnan(out->prev_close)) {
        out->prev_close = number_or_nan(cJSON_GetObjectItem(meta, "previousClose"));
    }
    double t = number_or_nan(cJSON_GetObjectItem(meta, "regularMarketTime"));
    out->time = isnan(t) ? 0 : (time_t)t;

    /* Day range and volume from the intraday bars. Yahoo's meta fields for these read 0 after the close. */
    const cJSON *quote = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(result, "indicators"), "quote"), 0);
    const cJSON *opens = cJSON_GetObjectItem(quote, "open");
    const cJSON *highs = cJSON_GetObjectItem(quote, "high");
    const cJSON *lows = cJSON_GetObjectItem(quote, "low");
    const cJSON *closes = cJSON_GetObjectItem(quote, "close");
    const cJSON *volumes = cJSON_GetObjectItem(quote, "volume");
    out->open = out->high = out->low = NAN;
    const cJSON *v = NULL;
    cJSON_ArrayForEach(v, opens) {
        if (cJSON_IsNumber(v)) {
            out->open = v->valuedouble;
            break;
        }
    }
    cJSON_ArrayForEach(v, highs) {
        if (cJSON_IsNumber(v) && (isnan(out->high) || v->valuedouble > out->high)) {
            out->high = v->valuedouble;
        }
    }
    cJSON_ArrayForEach(v, lows) {
        if (cJSON_IsNumber(v) && (isnan(out->low) || v->valuedouble < out->low)) {
            out->low = v->valuedouble;
        }
    }
    cJSON_ArrayForEach(v, volumes) {
        if (cJSON_IsNumber(v)) {
            out->volume += v->valuedouble;
        }
    }

    /* Intraday closes, thinned out evenly if there are more than the model keeps */
    int total = 0;
    cJSON_ArrayForEach(v, closes) {
        total += cJSON_IsNumber(v);
    }
    int idx = 0;
    cJSON_ArrayForEach(v, closes) {
        if (!cJSON_IsNumber(v)) {
            continue;
        }
        int slot = total <= STOCK_SERIES_MAX ? idx : (int)((int64_t)idx * (STOCK_SERIES_MAX - 1) / (total - 1));
        out->series[slot] = (float)v->valuedouble;
        out->series_len = slot + 1;
        idx++;
    }
    if (isnan(out->price) && out->series_len > 0) {
        out->price = out->series[out->series_len - 1];
    }

    cJSON_Delete(root);
    return true;
}

/*
 * TradingView scan: {"data": [{"s": "BMFBOVESPA:DI11!", "d": [close, change_abs, open, high, low,
 * volume, update_time]}, ...]}, the "d" values in the order of TV_COLUMNS. Returns the number of
 * quotes stored, -1 on bad JSON.
 */
#define TV_COLUMNS "\"close\",\"change_abs\",\"open\",\"high\",\"low\",\"volume\",\"update_time\""

static int parse_tradingview(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return -1;
    }
    int count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, cJSON_GetObjectItem(root, "data")) {
        const cJSON *symbol = cJSON_GetObjectItem(item, "s");
        const cJSON *d = cJSON_GetObjectItem(item, "d");
        double last = number_or_nan(cJSON_GetArrayItem(d, 0));
        double change = number_or_nan(cJSON_GetArrayItem(d, 1));
        double t = number_or_nan(cJSON_GetArrayItem(d, 6));
        if (cJSON_IsString(symbol) && !isnan(last) && !isnan(change)) {
            model_set_quote(symbol->valuestring, last, last - change, isnan(t) ? 0 : (time_t)t);
            model_set_detail(symbol->valuestring, NULL, number_or_nan(cJSON_GetArrayItem(d, 2)),
                             number_or_nan(cJSON_GetArrayItem(d, 3)), number_or_nan(cJSON_GetArrayItem(d, 4)),
                             number_or_nan(cJSON_GetArrayItem(d, 5)), NULL, 0);
            count++;
        }
    }
    cJSON_Delete(root);
    return count;
}

static void apply_chart(const char *symbol, const chart_data_t *c)
{
    if (!isnan(c->price) && !isnan(c->prev_close)) {
        model_set_quote(symbol, c->price, c->prev_close, c->time);
    }
    model_set_detail(symbol, c->name, c->open, c->high, c->low, c->volume, c->series, c->series_len);
}

/* ---------------------------------------------------------------------------------------------- */
/* Fetch steps (all run in the quotes task)                                                        */
/* ---------------------------------------------------------------------------------------------- */

/* Returns the HTTP status of the chart request for one symbol */
static int fetch_detail(const char *symbol)
{
    char url[128] = BASE_URL "/v8/finance/chart/";
    append_symbol(url, sizeof(url), symbol);
    strlcat(url, "?range=1d&interval=15m", sizeof(url));

    int status = http_request(url, NULL);
    if (status == 404) {
        model_set_not_found(symbol);
    } else if (status == 200 && parse_chart(s_resp.data, &s_chart)) {
        apply_chart(symbol, &s_chart);
    }
    return status;
}

/* The Yahoo symbols of the snapshot, so batches and round-robin can skip the other sources */
static int yahoo_symbols(int n, const char *out[])
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (s_list[i].source == STOCK_SOURCE_YAHOO) {
            out[count++] = s_list[i].symbol;
        }
    }
    return count;
}

/* One spark request per batch of symbols. Returns the HTTP status of the first failure, or 200. */
static int refresh_quotes(const char *symbols[], int n, int *quotes_out)
{
    static char url[URL_MAX];
    *quotes_out = 0;
    for (int start = 0; start < n; start += SPARK_BATCH) {
        strlcpy(url, BASE_URL "/v8/finance/spark?symbols=", sizeof(url));
        for (int i = start; i < n && i < start + SPARK_BATCH; i++) {
            if (i > start) {
                strlcat(url, ",", sizeof(url));
            }
            append_symbol(url, sizeof(url), symbols[i]);
        }
        strlcat(url, "&range=1d&interval=1d", sizeof(url));

        int status = http_request(url, NULL);
        if (status != 200) {
            return status;
        }
        int count = parse_spark(s_resp.data);
        if (count < 0) {
            return -2;
        }
        *quotes_out += count;
    }
    return 200;
}

/* One TradingView request for all its symbols. A failure is only logged: those rows go stale. */
static int refresh_tradingview(int n)
{
    static char body[64 + STOCK_MAX * (STOCK_SYMBOL_LEN + 3) + sizeof(TV_COLUMNS)];
    strlcpy(body, "{\"symbols\":{\"tickers\":[", sizeof(body));
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (s_list[i].source == STOCK_SOURCE_TRADINGVIEW) {
            strlcat(body, count++ ? ",\"" : "\"", sizeof(body));
            strlcat(body, s_list[i].symbol, sizeof(body));
            strlcat(body, "\"", sizeof(body));
        }
    }
    if (count == 0) {
        return 0;
    }
    strlcat(body, "]},\"columns\":[" TV_COLUMNS "]}", sizeof(body));

    int status = http_request(TV_SCAN_URL, body);
    int quotes = status == 200 ? parse_tradingview(s_resp.data) : -1;
    if (quotes < 0) {
        ESP_LOGW(TAG, "TradingView refresh failed (HTTP %d)", status);
        return 0;
    }
    return quotes;
}

/* Returns the time to wait before the next refresh */
static TickType_t refresh_cycle(void)
{
    static int round_robin;
    static const char *yahoo[STOCK_MAX];
    int64_t t0 = esp_timer_get_time();
    int n = model_snapshot(s_list, STOCK_MAX);
    int ny = yahoo_symbols(n, yahoo);
    /* TradingView first, so the Yahoo connection stays open for the detail requests that follow */
    int quotes = refresh_tradingview(n);
    int yahoo_quotes = 0;
    int status = ny > 0 ? refresh_quotes(yahoo, ny, &yahoo_quotes) : 200;
    quotes += yahoo_quotes;

    if (status != 200) {
        char error[48];
        if (status == 429) {
            snprintf(error, sizeof(error), "Yahoo rate limit, retrying in %d s", BACKOFF_S);
        } else if (status == -1) {
            snprintf(error, sizeof(error), "Network error");
        } else if (status == -2) {
            snprintf(error, sizeof(error), "Unexpected response");
        } else {
            snprintf(error, sizeof(error), "Yahoo HTTP %d", status);
        }
        ESP_LOGW(TAG, "Refresh failed: %s", error);
        model_set_refresh_result(false, error);
        return pdMS_TO_TICKS((status == 429 ? BACKOFF_S : settings_get_refresh_s()) * 1000);
    }
    model_set_refresh_result(true, NULL);

    /* Detail: every stock still missing it (first cycles), otherwise one per cycle, round-robin */
    int details = 0;
    for (int i = 0; i < n; i++) {
        if (s_list[i].source == STOCK_SOURCE_YAHOO && !s_list[i].has_detail && !s_list[i].not_found) {
            fetch_detail(s_list[i].symbol);
            details++;
        }
    }
    if (details == 0 && ny > 0) {
        fetch_detail(yahoo[round_robin++ % ny]);
        details = 1;
    }

    ESP_LOGI(TAG, "Refreshed %d/%d quotes + %d detail in %lld ms", quotes, n, details,
             (esp_timer_get_time() - t0) / 1000);
    return pdMS_TO_TICKS(settings_get_refresh_s() * 1000);
}

static void quotes_task(void *arg)
{
    TickType_t next_refresh = xTaskGetTickCount();
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = (int32_t)(next_refresh - now) > 0 ? next_refresh - now : 0;
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);

        if (!wifi_is_connected()) {
            /* Connecting to Wi-Fi sends NOTIFY_REFRESH, which ends this wait early */
            next_refresh = xTaskGetTickCount() + pdMS_TO_TICKS(5000);
            continue;
        }
        if (bits & NOTIFY_DETAIL) {
            char symbol[STOCK_SYMBOL_LEN];
            portENTER_CRITICAL(&s_req_lock);
            strlcpy(symbol, s_detail_request, sizeof(symbol));
            portEXIT_CRITICAL(&s_req_lock);
            /* TradingView rows get their day range with every refresh, and have no chart */
            if (symbol[0] && model_get(symbol, &s_one) && s_one.source == STOCK_SOURCE_YAHOO) {
                fetch_detail(symbol);
            }
        }
        if ((bits & NOTIFY_REFRESH) || (int32_t)(xTaskGetTickCount() - next_refresh) >= 0) {
            next_refresh = xTaskGetTickCount() + refresh_cycle();
        }
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* Public API                                                                                      */
/* ---------------------------------------------------------------------------------------------- */

void quotes_start(void)
{
    cJSON_Hooks hooks = {.malloc_fn = psram_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);

    s_resp.data = heap_caps_malloc(RESPONSE_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s_resp.data);
    /* TLS needs a deep stack; keep it in internal RAM */
    BaseType_t ok = xTaskCreate(quotes_task, "quotes", 12 * 1024, NULL, 3, &s_task);
    assert(ok == pdPASS);
}

void quotes_refresh_now(void)
{
    if (s_task) {
        xTaskNotify(s_task, NOTIFY_REFRESH, eSetBits);
    }
}

void quotes_request_detail(const char *symbol)
{
    portENTER_CRITICAL(&s_req_lock);
    strlcpy(s_detail_request, symbol, sizeof(s_detail_request));
    portEXIT_CRITICAL(&s_req_lock);
    if (s_task) {
        xTaskNotify(s_task, NOTIFY_DETAIL, eSetBits);
    }
}
