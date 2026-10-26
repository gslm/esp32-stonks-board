#include <stdbool.h>
#include <string.h>
#include "banner_messages.h"

/* Added by EMBED_TXTFILES in main/CMakeLists.txt; the texts are NUL-terminated */
extern const char money_txt[] asm("_binary_money_quotes_txt_start");
extern const char circuit_breaker_txt[] asm("_binary_circuit_breaker_quotes_txt_start");

static const char *text_of(banner_set_t set)
{
    return set == BANNER_CIRCUIT_BREAKER ? circuit_breaker_txt : money_txt;
}

/* Finds the start and length of the i-th non-empty line; returns false past the last one */
static bool find_line(const char *text, int i, const char **start, size_t *len)
{
    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n > 0 && p[n - 1] == '\r') {
            n--;
        }
        if (n > 0 && i-- == 0) {
            *start = p;
            *len = n;
            return true;
        }
        if (!end) {
            break;
        }
        p = end + 1;
    }
    return false;
}

int banner_message_count(banner_set_t set)
{
    static int counts[] = {-1, -1};
    int *count = &counts[set == BANNER_CIRCUIT_BREAKER];
    if (*count < 0) {
        const char *start;
        size_t len;
        *count = 0;
        while (find_line(text_of(set), *count, &start, &len)) {
            (*count)++;
        }
    }
    return *count;
}

void banner_message_get(banner_set_t set, int i, char *buf, size_t len)
{
    const char *start;
    size_t n;
    if (len == 0) {
        return;
    }
    if (!find_line(text_of(set), i, &start, &n)) {
        n = 0;
    }
    if (n >= len) {
        n = len - 1;
    }
    memcpy(buf, start, n);
    buf[n] = '\0';
}
