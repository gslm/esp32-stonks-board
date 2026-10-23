/*
 * Watchlist data model: the list of stocks, their latest quotes, and the app status.
 * The list itself is fixed (DEFAULT_STOCKS in stock_model.c); edit it there and rebuild.
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
#define STOCK_SYMBOL_LEN 20   /* Yahoo symbol, e.g. "VALE3.SA" */
#define STOCK_NAME_LEN   64
#define STOCK_SERIES_MAX 40   /* intraday closes kept for the detail chart */

typedef struct {
    char symbol[STOCK_SYMBOL_LEN]; /* Yahoo symbol. B3 tickers carry ".SA"; see stock_display_symbol() */
    char name[STOCK_NAME_LEN];

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

    bool not_found; /* Yahoo has no data for this symbol (e.g. delisted) */
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

/* Fills the model with the watchlist (the fixed list in stock_model.c). */
void model_init(void);

/*
 * Copies for readers. model_snapshot returns the number of stocks copied.
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

void model_get_status(app_status_t *out);
void model_set_net_state(net_state_t state);
void model_set_refresh_result(bool ok, const char *error);

/* Yahoo symbol as shown on screen ("VALE3.SA" -> "VALE3") */
const char *stock_display_symbol(const char *symbol, char *buf, size_t buf_len);
