/*
 * Data model: the watchlist stocks, the market indicators, their latest quotes, and the app status.
 * Both lists are fixed (DEFAULT_STOCKS and MARKET in stock_model.c); edit them there and rebuild.
 *
 * Shared by three tasks (LVGL, quote fetcher, console), so every access goes through a
 * mutex inside this module. Readers take copies (model_snapshot / model_get) and compare
 * version counters to see whether anything changed since they last looked.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define STOCK_MAX        24
#define STOCK_SYMBOL_LEN 24   /* data source symbol, e.g. "VALE3.SA" or "BMFBOVESPA:DI1F2028" */
#define STOCK_TICKER_LEN 12   /* as shown on screen, e.g. "VALE3" */
#define STOCK_NAME_LEN   64
#define STOCK_SERIES_MAX 40   /* intraday closes kept for the detail chart */

typedef enum {
    STOCK_GROUP_WATCHLIST, /* "Stocks" page */
    STOCK_GROUP_MARKET,    /* "Market" page: index, dollar, ETF, interest rate futures */
} stock_group_t;

/* How the price is printed */
typedef enum {
    STOCK_FMT_PRICE, /* 61.49 */
    STOCK_FMT_INDEX, /* 205,083 */
    STOCK_FMT_FX,    /* 4.9652 */
    STOCK_FMT_RATE,  /* 13.47% */
} stock_format_t;

typedef enum {
    STOCK_SOURCE_YAHOO,
    STOCK_SOURCE_TRADINGVIEW, /* no intraday chart */
} stock_source_t;

typedef struct {
    char symbol[STOCK_SYMBOL_LEN]; /* symbol at the data source. B3 tickers on Yahoo carry ".SA" */
    char ticker[STOCK_TICKER_LEN];
    char name[STOCK_NAME_LEN];
    stock_group_t group;
    stock_format_t format;
    stock_source_t source;

    /* Fast refresh: all symbols in one request */
    bool has_quote;
    double last;
    double prev_close;
    double change;
    double change_pct;
    time_t quote_time; /* time of the last trade */

    /* Slower per-symbol refresh: day range, volume and the intraday series */
    bool has_detail;
    double open;
    double high;
    double low;
    double volume;
    uint8_t series_len;
    float series[STOCK_SERIES_MAX];

    bool not_found; /* the source has no data for this symbol (e.g. delisted) */
} stock_t;

typedef enum {
    NET_NOT_CONFIGURED, /* no Wi-Fi credentials yet */
    NET_CONNECTING,
    NET_ONLINE,
} net_state_t;

typedef struct {
    net_state_t net;
    time_t last_update; /* end of the last successful refresh, 0 = never */
    char error[64];     /* why the last refresh failed, "" if it worked */
} app_status_t;

/* Fills the model with the watchlist and the market indicators (the fixed lists in stock_model.c). */
void model_init(void);

/*
 * Copies for readers. model_snapshot returns the number of stocks copied (both groups, watchlist first).
 * Symbols match case-insensitively, with or without the ".SA" suffix ("vale3" finds "VALE3.SA").
 */
int model_snapshot(stock_t *out, int max);
bool model_get(const char *symbol, stock_t *out);
int model_count(void);

uint32_t model_list_version(void); /* changes when the set of stocks changes (only at start-up today) */
uint32_t model_data_version(void); /* changes on any change, including prices and status */

/* Updates from the quote fetcher (not saved to NVS). */
void model_set_quote(const char *symbol, double last, double prev_close, time_t quote_time);
void model_set_detail(const char *symbol, const char *name, double open, double high, double low,
                      double volume, const float *series, int series_len);
void model_set_not_found(const char *symbol);

/*
 * Circuit breaker: B3 halts trading when the Ibovespa falls 10 % below the previous close
 * (30 min; at 15 %: 1 h; at 20 %: B3 decides). No free source publishes the halt itself, so this
 * is true while the Ibovespa is at CIRCUIT_BREAKER_PCT or lower, or while the "cbtest" console
 * command forces it.
 */
#define CIRCUIT_BREAKER_PCT -10.0
bool model_circuit_breaker(void);
void model_set_circuit_breaker_test(bool on);
bool model_circuit_breaker_test(void);

void model_get_status(app_status_t *out);
void model_set_net_state(net_state_t state);
void model_set_refresh_result(bool ok, const char *error);
