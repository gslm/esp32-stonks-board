/* Detail screen for one stock or market row: price, change, intraday chart and day stats. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "quotes.h"
#include "stock_model.h"
#include "ui_screens.h"
#include "ui_theme.h"

enum { STAT_OPEN, STAT_HIGH, STAT_LOW, STAT_PREV, STAT_VOLUME, STAT_TIME, STAT_COUNT };

static lv_obj_t *s_screen;
static lv_obj_t *s_symbol;
static lv_obj_t *s_name;
static lv_obj_t *s_price;
static lv_obj_t *s_change;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;
static lv_obj_t *s_chart_note;
static lv_obj_t *s_stats[STAT_COUNT];
static char s_id[STOCK_SYMBOL_LEN]; /* data source symbol on display */
static uint32_t s_data_version = UINT32_MAX;
static stock_t s_stock;

static void go_back(void)
{
    lv_screen_load(ui_watchlist_screen());
}

static void on_back_clicked(lv_event_t *e)
{
    go_back();
}

static void create_header(void)
{
    lv_obj_t *header = ui_box_create(s_screen);
    lv_obj_set_size(header, 480, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = ui_label_create(back, &font_20, UI_TEXT_PRIMARY);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_center(arrow);

    s_symbol = ui_label_create(header, &font_24, UI_GOLD);
    lv_obj_align(s_symbol, LV_ALIGN_LEFT_MID, 60, 0);

    s_name = ui_label_create(header, &font_14, UI_TEXT_SECONDARY);
    ui_label_max_lines(s_name, 260, 1);
    lv_obj_align_to(s_name, s_symbol, LV_ALIGN_OUT_RIGHT_MID, 12, 2);
}

static void create_chart(void)
{
    s_chart = lv_chart_create(s_screen);
    lv_obj_set_size(s_chart, 448, 200);
    lv_obj_set_pos(s_chart, 16, 148);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 4, 0);
    lv_obj_set_style_bg_color(s_chart, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_chart, 8, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 12, 0);
    lv_obj_set_style_line_color(s_chart, UI_DIVIDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    /* No point markers, just the line */
    lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
    s_series = lv_chart_add_series(s_chart, UI_ACCENT, LV_CHART_AXIS_PRIMARY_Y);

    s_chart_note = ui_label_create(s_chart, &font_14, UI_TEXT_SECONDARY);
    lv_obj_center(s_chart_note);
}

static void create_stats(void)
{
    static const char *const CAPTIONS[STAT_COUNT] = {"Open", "High", "Low", "Prev close", "Volume", "Time"};
    for (int i = 0; i < STAT_COUNT; i++) {
        lv_obj_t *cell = ui_box_create(s_screen);
        lv_obj_set_size(cell, 149, 48);
        lv_obj_set_pos(cell, 16 + (i % 3) * 149, 360 + (i / 3) * 48);
        lv_obj_t *caption = ui_label_create(cell, &font_14, UI_TEXT_SECONDARY);
        lv_label_set_text(caption, CAPTIONS[i]);
        s_stats[i] = ui_label_create(cell, &font_16, UI_TEXT_PRIMARY);
        lv_obj_set_pos(s_stats[i], 0, 20);
    }
}

void ui_detail_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, UI_BG_SCREEN, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header();
    s_price = ui_label_create(s_screen, &font_40, UI_TEXT_PRIMARY);
    lv_obj_set_pos(s_price, 16, 64);
    s_change = ui_label_create(s_screen, &font_20, UI_FLAT);
    lv_obj_set_pos(s_change, 16, 112);
    create_chart();
    create_stats();
}

static void update_chart(const stock_t *s, lv_color_t color)
{
    if (s->series_len < 2) {
        lv_chart_set_point_count(s_chart, 1);
        lv_chart_set_all_values(s_chart, s_series, LV_CHART_POINT_NONE);
        const char *note = "Loading intraday chart...";
        if (s->source == STOCK_SOURCE_TRADINGVIEW) {
            note = "No intraday chart for this one";
        } else if (s->not_found) {
            note = "No data on Yahoo Finance";
        }
        ui_set_text(s_chart_note, note);
        lv_obj_remove_flag(s_chart_note, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_chart_note, LV_OBJ_FLAG_HIDDEN);

    /* Chart values are integers: use cents, or 1/10000 for the dollar, which moves in small steps */
    float scale = s->format == STOCK_FMT_FX ? 10000.0f : 100.0f;
    float lo = s->series[0];
    float hi = s->series[0];
    for (int i = 1; i < s->series_len; i++) {
        lo = fminf(lo, s->series[i]);
        hi = fmaxf(hi, s->series[i]);
    }
    int32_t lo_c = (int32_t)floorf(lo * scale);
    int32_t hi_c = (int32_t)ceilf(hi * scale);
    int32_t pad = (hi_c - lo_c) / 10 + 1;
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo_c - pad, hi_c + pad);
    lv_chart_set_point_count(s_chart, s->series_len);
    for (int i = 0; i < s->series_len; i++) {
        lv_chart_set_series_value_by_id(s_chart, s_series, i, (int32_t)lroundf(s->series[i] * scale));
    }
    lv_chart_set_series_color(s_chart, s_series, s->has_quote ? color : UI_ACCENT);
    lv_chart_refresh(s_chart);
}

void ui_detail_update(void)
{
    if (lv_screen_active() != s_screen) {
        return;
    }
    uint32_t version = model_data_version();
    if (version == s_data_version) {
        return;
    }
    s_data_version = version;
    if (!model_get(s_id, &s_stock)) {
        go_back(); /* not in the list (cannot happen with the fixed list, but stay safe) */
        return;
    }
    const stock_t *s = &s_stock;
    char buf[STOCK_NAME_LEN];
    char tmp[24];
    char tmp2[24];

    ui_set_text(s_symbol, s->ticker);
    lv_obj_set_style_text_color(s_symbol, ui_ticker_color(s), 0);
    lv_obj_align_to(s_name, s_symbol, LV_ALIGN_OUT_RIGHT_MID, 12, 2);
    ui_set_text(s_name, s->name);

    ui_fmt_price(buf, sizeof(buf), s->last, s->has_quote, s->format);
    ui_set_text(s_price, buf);
    ui_fmt_change(tmp, sizeof(tmp), s->change, s->has_quote, s->format);
    ui_fmt_pct(tmp2, sizeof(tmp2), s->change_pct, s->has_quote);
    snprintf(buf, sizeof(buf), "%s  (%s)", tmp, tmp2);
    ui_set_text(s_change, s->has_quote ? buf : "--");
    /* By the % change: it decides the colour the same way for every format */
    lv_color_t color = ui_trend_color(s->change_pct, s->has_quote);
    lv_obj_set_style_text_color(s_change, color, 0);

    ui_fmt_price(buf, sizeof(buf), s->open, s->has_detail, s->format);
    ui_set_text(s_stats[STAT_OPEN], buf);
    ui_fmt_price(buf, sizeof(buf), s->high, s->has_detail, s->format);
    ui_set_text(s_stats[STAT_HIGH], buf);
    ui_fmt_price(buf, sizeof(buf), s->low, s->has_detail, s->format);
    ui_set_text(s_stats[STAT_LOW], buf);
    ui_fmt_price(buf, sizeof(buf), s->prev_close, s->has_quote, s->format);
    ui_set_text(s_stats[STAT_PREV], buf);
    ui_fmt_volume(buf, sizeof(buf), s->volume, s->has_detail);
    ui_set_text(s_stats[STAT_VOLUME], buf);
    ui_fmt_time(buf, sizeof(buf), s->has_quote ? s->quote_time : 0, false);
    ui_set_text(s_stats[STAT_TIME], buf);

    update_chart(s, color);
}

void ui_detail_open(const char *symbol)
{
    strlcpy(s_id, symbol, sizeof(s_id));
    s_data_version = UINT32_MAX; /* force a full refresh */
    lv_screen_load(s_screen);
    ui_detail_update();
    quotes_request_detail(symbol);
}
