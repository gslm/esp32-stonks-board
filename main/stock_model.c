#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "stock_model.h"

static const char *TAG = "model";

typedef struct {
    const char *symbol;
    const char *name;
} default_stock_t;

/* The "Ativos" watchlist from Investing.com, with the names it shows. Edit here to change the list. */
static const default_stock_t DEFAULT_STOCKS[] = {
    {"VAMO3.SA", "Vamos Locacao"},
    {"WIZC3.SA", "Wiz Co Participacoes e Corretagem de Seguros"},
    {"HASH11.SA", "Hashdex Nasdaq Crypto"},
    {"BBAS3.SA", "Banco Do Brasil"},
    {"TAEE11.SA", "TAEE UNIT"},
    {"CPLE3.SA", "COPEL ON"},
    {"ITSA4.SA", "Itausa Investimentos Itau Pref"},
    {"SNCI11.SA", "FII Suno Recebíveis Imobiliários"},
    {"EGIE3.SA", "ENGIE Brasil Energia"},
    {"VALE3.SA", "Vale SA"},
    {"WEGE3.SA", "WEG ON"},
    {"IVVB11.SA", "Fundo de Invest Ishares SP 500"},
    {"BBSE3.SA", "BB Seguridade"},
};

typedef struct {
    const char *symbol;
    const char *ticker;
    const char *name;
    stock_format_t format;
    stock_source_t source;
} market_item_t;

/* The "Market" page. DI futures are not on Yahoo; TradingView's public scanner has them. */
static const market_item_t MARKET[] = {
    {"^BVSP", "IBOV", "Ibovespa index", STOCK_FMT_INDEX, STOCK_SOURCE_YAHOO},
    {"BRL=X", "USD/BRL", "US dollar in Brazilian reais", STOCK_FMT_FX, STOCK_SOURCE_YAHOO},
    {"EWZ", "EWZ", "iShares MSCI Brazil ETF (NYSE, US$)", STOCK_FMT_PRICE, STOCK_SOURCE_YAHOO},
    {"BMFBOVESPA:DI11!", "DI11!", "DI rate futures, nearest contract (% a year)", STOCK_FMT_RATE, STOCK_SOURCE_TRADINGVIEW},
    {"BMFBOVESPA:DI1F2028", "DI1F28", "DI rate futures, January 2028 (% a year)", STOCK_FMT_RATE, STOCK_SOURCE_TRADINGVIEW},
    {"BMFBOVESPA:DI1F2029", "DI1F29", "DI rate futures, January 2029 (% a year)", STOCK_FMT_RATE, STOCK_SOURCE_TRADINGVIEW},
    {"BMFBOVESPA:DI1F2030", "DI1F30", "DI rate futures, January 2030 (% a year)", STOCK_FMT_RATE, STOCK_SOURCE_TRADINGVIEW},
};

static SemaphoreHandle_t s_mutex;
static stock_t s_stocks[STOCK_MAX];
static int s_count;
static uint32_t s_list_version;
static uint32_t s_data_version;
static app_status_t s_status;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

/* ---------------------------------------------------------------------------------------------- */
/* Symbol helpers                                                                                  */
/* ---------------------------------------------------------------------------------------------- */

/* Ticker shown on screen: the Yahoo symbol without ".SA" ("VALE3.SA" -> "VALE3") */
static void ticker_from_symbol(const char *symbol, char *buf, size_t buf_len)
{
    size_t len = strlen(symbol);
    if (len > 3 && strcasecmp(symbol + len - 3, ".SA") == 0) {
        len -= 3;
    }
    if (len >= buf_len) {
        len = buf_len - 1;
    }
    memcpy(buf, symbol, len);
    buf[len] = '\0';
}

/* Matches "VALE3.SA" against "VALE3.SA", "vale3.sa" or "VALE3" */
static bool symbol_matches(const char *stored, const char *query)
{
    if (strcasecmp(stored, query) == 0) {
        return true;
    }
    size_t qlen = strlen(query);
    return strncasecmp(stored, query, qlen) == 0 && strcasecmp(stored + qlen, ".SA") == 0;
}

static int find_locked(const char *symbol)
{
    for (int i = 0; i < s_count; i++) {
        if (symbol_matches(s_stocks[i].symbol, symbol)) {
            return i;
        }
    }
    return -1;
}

static stock_t *add_locked(const char *symbol, const char *name, stock_group_t group)
{
    stock_t *s = &s_stocks[s_count++];
    memset(s, 0, sizeof(*s));
    strlcpy(s->symbol, symbol, sizeof(s->symbol));
    ticker_from_symbol(symbol, s->ticker, sizeof(s->ticker));
    strlcpy(s->name, name, sizeof(s->name));
    s->group = group;
    return s;
}

void model_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    assert(s_mutex);
    LOCK();
    for (size_t i = 0; i < sizeof(DEFAULT_STOCKS) / sizeof(DEFAULT_STOCKS[0]) && s_count < STOCK_MAX; i++) {
        add_locked(DEFAULT_STOCKS[i].symbol, DEFAULT_STOCKS[i].name, STOCK_GROUP_WATCHLIST);
    }
    int stocks = s_count;
    for (size_t i = 0; i < sizeof(MARKET) / sizeof(MARKET[0]) && s_count < STOCK_MAX; i++) {
        stock_t *s = add_locked(MARKET[i].symbol, MARKET[i].name, STOCK_GROUP_MARKET);
        strlcpy(s->ticker, MARKET[i].ticker, sizeof(s->ticker));
        s->format = MARKET[i].format;
        s->source = MARKET[i].source;
    }
    ESP_LOGI(TAG, "Watchlist: %d stocks, market: %d", stocks, s_count - stocks);
    s_status.net = NET_NOT_CONFIGURED;
    s_list_version++;
    s_data_version++;
    UNLOCK();
}

/* ---------------------------------------------------------------------------------------------- */
/* Readers                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

int model_snapshot(stock_t *out, int max)
{
    LOCK();
    int n = s_count < max ? s_count : max;
    memcpy(out, s_stocks, (size_t)n * sizeof(stock_t));
    UNLOCK();
    return n;
}

bool model_get(const char *symbol, stock_t *out)
{
    LOCK();
    int i = find_locked(symbol);
    if (i >= 0) {
        *out = s_stocks[i];
    }
    UNLOCK();
    return i >= 0;
}

int model_count(void)
{
    LOCK();
    int n = s_count;
    UNLOCK();
    return n;
}

uint32_t model_list_version(void)
{
    LOCK();
    uint32_t v = s_list_version;
    UNLOCK();
    return v;
}

uint32_t model_data_version(void)
{
    LOCK();
    uint32_t v = s_data_version;
    UNLOCK();
    return v;
}

/* ---------------------------------------------------------------------------------------------- */
/* Updates from the fetcher                                                                        */
/* ---------------------------------------------------------------------------------------------- */

void model_set_quote(const char *symbol, double last, double prev_close, time_t quote_time)
{
    LOCK();
    int i = find_locked(symbol);
    if (i >= 0) {
        stock_t *s = &s_stocks[i];
        s->has_quote = true;
        s->not_found = false;
        s->last = last;
        s->prev_close = prev_close;
        s->change = prev_close > 0 ? last - prev_close : 0;
        s->change_pct = prev_close > 0 ? s->change / prev_close * 100.0 : 0;
        s->quote_time = quote_time;
        s_data_version++;
    }
    UNLOCK();
}

void model_set_detail(const char *symbol, const char *name, double open, double high, double low,
                      double volume, const float *series, int series_len)
{
    LOCK();
    int i = find_locked(symbol);
    if (i >= 0) {
        stock_t *s = &s_stocks[i];
        /* Keep the Investing.com names from the list; Yahoo's name only fills a missing one */
        if (name && name[0] && s->name[0] == '\0') {
            strlcpy(s->name, name, sizeof(s->name));
        }
        s->has_detail = true;
        s->not_found = false;
        s->open = open;
        s->high = high;
        s->low = low;
        s->volume = volume;
        if (series_len > STOCK_SERIES_MAX) {
            series_len = STOCK_SERIES_MAX;
        }
        if (series_len > 0) {
            memcpy(s->series, series, (size_t)series_len * sizeof(float));
        }
        s->series_len = (uint8_t)series_len;
        s_data_version++;
    }
    UNLOCK();
}

void model_set_not_found(const char *symbol)
{
    LOCK();
    int i = find_locked(symbol);
    if (i >= 0 && !s_stocks[i].not_found) {
        s_stocks[i].not_found = true;
        s_data_version++;
    }
    UNLOCK();
}

void model_get_status(app_status_t *out)
{
    LOCK();
    *out = s_status;
    UNLOCK();
}

void model_set_net_state(net_state_t state)
{
    LOCK();
    if (s_status.net != state) {
        s_status.net = state;
        s_data_version++;
    }
    UNLOCK();
}

void model_set_refresh_result(bool ok, const char *error)
{
    LOCK();
    if (ok) {
        s_status.last_update = time(NULL);
        s_status.error[0] = '\0';
    } else {
        strlcpy(s_status.error, error ? error : "error", sizeof(s_status.error));
    }
    s_data_version++;
    UNLOCK();
}
