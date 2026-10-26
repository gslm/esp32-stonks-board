/*
 * Quotes for the rolling banner. They come from two text files at the repo root, which the build
 * embeds into the firmware: one quote per line, empty lines are skipped. Edit them and rebuild.
 *   - money_quotes.txt: the usual ones
 *   - circuit_breaker_quotes.txt: shown instead during a circuit breaker
 */
#pragma once

#include <stddef.h>

typedef enum {
    BANNER_MONEY,
    BANNER_CIRCUIT_BREAKER,
} banner_set_t;

int banner_message_count(banner_set_t set);
/* Copies quote i (0 .. count-1) of the set into buf */
void banner_message_get(banner_set_t set, int i, char *buf, size_t len);
