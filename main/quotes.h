/*
 * Quote fetcher: one FreeRTOS task that does all the network I/O and writes into the model.
 *
 * Data source: Yahoo Finance's public chart endpoints (no API key). B3 quotes there are
 * delayed about 15 minutes, the same as on Investing.com's free pages.
 *   - Every refresh interval: one "spark" request for all symbols -> last price, previous close
 *   - Round-robin, and on demand: one "chart" request per symbol -> open/high/low/volume, name,
 *     and the intraday series for the detail chart
 * The DI rate futures (Market page) are not on Yahoo: they come from TradingView's public scanner
 * endpoint, one request per refresh with price, previous close and day range (no chart).
 *
 * Other tasks never block on the network: they post requests and read the model.
 */
#pragma once

#include <stdbool.h>
#include "stock_model.h"

void quotes_start(void);

/* Fetch all quotes now instead of waiting for the next interval. Safe to call before quotes_start(). */
void quotes_refresh_now(void);

/* Fetch the detail (day range, volume, intraday series) of one symbol soon. */
void quotes_request_detail(const char *symbol);
