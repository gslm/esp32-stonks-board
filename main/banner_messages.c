#include <stdbool.h>
#include <string.h>
#include "banner_messages.h"

/* Added by EMBED_TXTFILES in main/CMakeLists.txt; the text is NUL-terminated */
extern const char quotes_txt[] asm("_binary_money_quotes_txt_start");

/* Finds the start and length of the i-th non-empty line; returns false past the last one */
static bool find_line(int i, const char **start, size_t *len)
{
    const char *p = quotes_txt;
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

int banner_message_count(void)
{
    static int count = -1;
    if (count < 0) {
        const char *start;
        size_t len;
        count = 0;
        while (find_line(count, &start, &len)) {
            count++;
        }
    }
    return count;
}

void banner_message_get(int i, char *buf, size_t len)
{
    const char *start;
    size_t n;
    if (len == 0) {
        return;
    }
    if (!find_line(i, &start, &n)) {
        n = 0;
    }
    if (n >= len) {
        n = len - 1;
    }
    memcpy(buf, start, n);
    buf[n] = '\0';
}
