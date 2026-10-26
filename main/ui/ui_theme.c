#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_theme.h"

/* How each stock_format_t prints. The suffix is for the price only, not for the change. */
static const struct {
    int decimals;
    bool thousands; /* "," between groups of three digits */
    const char *suffix;
} FORMATS[] = {
    [STOCK_FMT_PRICE] = {2, false, ""},
    [STOCK_FMT_INDEX] = {0, true, ""},
    [STOCK_FMT_FX] = {4, false, ""},
    [STOCK_FMT_RATE] = {2, false, "%"},
};

void ui_theme_init(lv_display_t *disp)
{
    /* LVGL's default theme in dark mode, so built-in widgets (keyboard, message box) match */
    lv_theme_t *theme = lv_theme_default_init(disp, UI_ACCENT, UI_UP, true, &font_16);
    lv_display_set_theme(disp, theme);
}

lv_color_t ui_trend_color(double change, bool valid)
{
    if (!valid || isnan(change) || fabs(change) < 0.005) {
        return UI_FLAT;
    }
    return change > 0 ? UI_UP : UI_DOWN;
}

lv_color_t ui_ticker_color(const stock_t *s)
{
    return s->group == STOCK_GROUP_MARKET ? UI_PURPLE : UI_GOLD;
}

lv_obj_t *ui_label_create(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void ui_label_max_lines(lv_obj_t *label, int32_t width, int lines)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    int32_t line_space = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_obj_set_width(label, width);
    lv_obj_set_height(label, lines * lv_font_get_line_height(font) + (lines - 1) * line_space);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

lv_obj_t *ui_box_create(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

/* 205083 -> "205,083" */
static void fmt_thousands(char *buf, size_t len, long long v)
{
    if (v < 1000) {
        snprintf(buf, len, "%lld", v);
        return;
    }
    fmt_thousands(buf, len, v / 1000);
    size_t used = strlen(buf);
    snprintf(buf + used, len - used, ",%03lld", v % 1000);
}

/* value rounded to `decimals` (0..4) places, as sign + integer part + decimals + suffix */
static void fmt_fixed(char *buf, size_t len, double v, int decimals, bool thousands, const char *plus,
                      const char *suffix)
{
    static const long long SCALE[] = {1, 10, 100, 1000, 10000};
    decimals = LV_CLAMP(0, decimals, 4);
    long long scaled = llround(v * SCALE[decimals]);
    const char *sign = scaled < 0 ? "-" : (scaled > 0 ? plus : "");
    long long a = llabs(scaled);
    char whole[24];
    if (thousands) {
        fmt_thousands(whole, sizeof(whole), a / SCALE[decimals]);
    } else {
        snprintf(whole, sizeof(whole), "%lld", a / SCALE[decimals]);
    }
    if (decimals > 0) {
        snprintf(buf, len, "%s%s.%0*lld%s", sign, whole, decimals, a % SCALE[decimals], suffix);
    } else {
        snprintf(buf, len, "%s%s%s", sign, whole, suffix);
    }
}

static void fmt_fixed2(char *buf, size_t len, double v, const char *plus, const char *suffix)
{
    fmt_fixed(buf, len, v, 2, false, plus, suffix);
}

void ui_fmt_price(char *buf, size_t len, double v, bool valid, stock_format_t fmt)
{
    if (!valid || isnan(v)) {
        snprintf(buf, len, "--");
    } else {
        fmt_fixed(buf, len, v, FORMATS[fmt].decimals, FORMATS[fmt].thousands, "", FORMATS[fmt].suffix);
    }
}

void ui_fmt_change(char *buf, size_t len, double v, bool valid, stock_format_t fmt)
{
    if (!valid || isnan(v)) {
        snprintf(buf, len, "--");
    } else {
        fmt_fixed(buf, len, v, FORMATS[fmt].decimals, FORMATS[fmt].thousands, "+", "");
    }
}

void ui_fmt_pct(char *buf, size_t len, double v, bool valid)
{
    if (!valid || isnan(v)) {
        snprintf(buf, len, "--");
    } else {
        fmt_fixed2(buf, len, v, "+", "%");
    }
}

void ui_fmt_volume(char *buf, size_t len, double v, bool valid)
{
    if (!valid || isnan(v) || v <= 0) {
        snprintf(buf, len, "--");
    } else if (v >= 1e9) {
        fmt_fixed2(buf, len, v / 1e9, "", "B");
    } else if (v >= 1e6) {
        fmt_fixed2(buf, len, v / 1e6, "", "M");
    } else if (v >= 1e3) {
        fmt_fixed2(buf, len, v / 1e3, "", "K");
    } else {
        snprintf(buf, len, "%lld", llround(v));
    }
}

void ui_fmt_time(char *buf, size_t len, time_t t, bool seconds)
{
    struct tm tm;
    /* Before SNTP sets the clock, time() counts from 1970: show "--" instead of a bogus time */
    if (t < 1704067200 /* 2024-01-01 */ || localtime_r(&t, &tm) == NULL) {
        snprintf(buf, len, "--");
    } else if (seconds) {
        snprintf(buf, len, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
}
