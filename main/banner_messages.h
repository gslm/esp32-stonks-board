/*
 * Quotes for the rolling banner. They come from money_quotes.txt (repo root), which the build
 * embeds into the firmware: one quote per line, empty lines are skipped. Edit it and rebuild.
 */
#pragma once

#include <stddef.h>

int banner_message_count(void);
/* Copies quote i (0 .. count-1) into buf */
void banner_message_get(int i, char *buf, size_t len);
