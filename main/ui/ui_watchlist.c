/*
 * Main screen: a mood title ("Stonks!" / "Not stonks..."), column captions, and two pages,
 * swiped left and right (an LVGL tileview):
 *   1. Stocks: the watchlist.
 *   2. Market: index, dollar, ETF and interest rate futures.
 * Each page is an endless wheel that glides on its own (or scrolls by hand, still endlessly).
 * Auto-scroll is switched on or off per page in Settings > Scroll Options.
 *
 * How the endless wheel works: the list is an ordinary LVGL scrolling container with one row
 * per stock. Whenever a row leaves the view at one end, it is moved to the other end and the
 * scroll position is shifted by one row height, so nothing visibly jumps. The scroll position
 * therefore always stays between ROW_H and 2 * ROW_H, far from the edges, in both directions.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "settings.h"
#include "stock_model.h"
#include "ui_screens.h"
#include "ui_theme.h"

#define CAPTIONS_H    28
#define LIST_Y        (UI_HEADER_H + CAPTIONS_H)
#define LIST_H        (480 - LIST_Y)
#define ROW_H         92
#define MID_RIGHT     344 /* right edge of the price column */
#define NAME_W        250
#define FADE_TOP_H    12
#define FADE_BOTTOM_H 40

/* Badges after the ticker: the day's best performer above these thresholds, and the worst one
   below LOSER_PCT */
#define MONEYBAG_PCT 2.0
#define TOPHAT_PCT   10.0
#define LOSER_PCT    -2.0

/*
 * Auto-roll speed comes from Settings > Scroll Options (also the "speed" console command), in
 * tenths: 0 = stopped .. 10 = 1.0x, which moves one row per second. The list glides continuously.
 */
#define KICK_TICK_MS    100
#define TOUCH_RESUME_MS 5000 /* auto-roll waits this long after the list was last touched */
#define GEAR_X          352
#define WIFI_ICON_X     402

typedef struct {
    lv_obj_t *row;
    lv_obj_t *symbol;
    lv_obj_t *badge; /* money bag or top hat (best performer), flying money (worst), else hidden */
    lv_obj_t *name;
    lv_obj_t *last;
    lv_obj_t *change;
    lv_obj_t *pill;
    lv_obj_t *pct;
    char id[STOCK_SYMBOL_LEN]; /* data source symbol the row shows */
} row_t;

/* One page of the main screen: a list of rows that loops endlessly and glides on its own */
typedef struct {
    int index;          /* 0 = Stocks, 1 = Market, as in settings_get_autoscroll() */
    lv_obj_t *tile;     /* the page in the tileview */
    lv_obj_t *list;
    row_t rows[STOCK_MAX];
    int row_count;
    bool wheel;         /* enough rows to loop endlessly */
    bool pressed;       /* a finger is on the list right now */
    uint32_t touch_ms;  /* when the list was last touched */
} page_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_title_icon; /* chart up or down, after the title */
static lv_obj_t *s_wifi_icon;  /* green: online, yellow: Wi-Fi but no internet, red + slash: no Wi-Fi */
static lv_obj_t *s_wifi_slash;
static lv_obj_t *s_update_time; /* time of the last successful refresh */
static lv_obj_t *s_page_label;  /* "Stocks 1/2" or "Market 2/2" */
static lv_obj_t *s_tileview;    /* holds the pages side by side */
static page_t s_stocks = {.index = 0};
static page_t s_market = {.index = 1};
static uint32_t s_list_version = UINT32_MAX;
static uint32_t s_data_version = UINT32_MAX;
EXT_RAM_BSS_ATTR static stock_t s_snapshot[STOCK_MAX];

static uint32_t s_glide_last_ms;
static int32_t s_glide_rem;       /* sub-pixel remainder of the glide, in 1/1000 px */

static page_t *active_page(void)
{
    return lv_tileview_get_tile_active(s_tileview) == s_market.tile ? &s_market : &s_stocks;
}

/* ---------------------------------------------------------------------------------------------- */
/* Endless wheel                                                                                   */
/* ---------------------------------------------------------------------------------------------- */

/* Move rows between the ends until the scroll position is back in [ROW_H, 2 * ROW_H) */
static void wheel_normalize(page_t *p)
{
    static bool busy;
    /* LVGL's own scroll animations (snap to row) aim at an absolute position: let them finish */
    if (!p->wheel || busy || lv_anim_get(p->list, NULL) != NULL) {
        return;
    }
    busy = true;
    int32_t y = lv_obj_get_scroll_y(p->list);
    int32_t start = y;
    while (y >= 2 * ROW_H) {
        lv_obj_move_to_index(lv_obj_get_child(p->list, 0), -1); /* top row -> bottom */
        y -= ROW_H;
    }
    while (y < ROW_H) {
        lv_obj_move_to_index(lv_obj_get_child(p->list, -1), 0); /* bottom row -> top */
        y += ROW_H;
    }
    if (y != start) {
        lv_obj_update_layout(p->list);
        lv_obj_scroll_to_y(p->list, y, LV_ANIM_OFF);
    }
    busy = false;
}

static void on_list_scroll(lv_event_t *e)
{
    wheel_normalize(lv_event_get_user_data(e));
}

/* Touches on a list (rows bubble their events up to it) pause its auto-roll */
static void on_list_touch(lv_event_t *e)
{
    page_t *p = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    p->pressed = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING;
    p->touch_ms = lv_tick_get();
}

/* Only the page on screen glides, when its checkbox in Settings > Scroll Options is on */
static bool glide_allowed(const page_t *p, int tenths)
{
    return tenths > 0 && settings_get_autoscroll(p->index) && p->wheel && lv_screen_active() == s_screen &&
           !p->pressed && lv_tick_diff(lv_tick_get(), p->touch_ms) >= TOUCH_RESUME_MS &&
           lv_anim_get(p->list, NULL) == NULL;
}

/*
 * LVGL calls this at the start of every display refresh, before it collects what to redraw.
 * Moving the list here means one move per drawn frame, sized by the real time since the previous
 * frame, so the motion stays even when frame times vary.
 */
static void on_refresh_start(lv_event_t *e)
{
    uint32_t now = lv_tick_get();
    uint32_t elapsed = LV_MIN(lv_tick_diff(now, s_glide_last_ms), 100); /* no big jump after a stall */
    s_glide_last_ms = now;

    page_t *p = active_page();
    int tenths = settings_get_roll_speed();
    if (!glide_allowed(p, tenths)) {
        s_glide_rem = 0;
        return;
    }
    /* At 1.0x the list moves ROW_H px per second: that is ROW_H * tenths / 10 thousandths of a px per ms */
    int32_t milli_px = (int32_t)(elapsed * ROW_H * (uint32_t)tenths / 10) + s_glide_rem;
    s_glide_rem = milli_px % 1000;
    if (milli_px >= 1000) {
        lv_obj_scroll_by(p->list, 0, -(milli_px / 1000), LV_ANIM_OFF); /* negative dy = content moves up */
    }
}

/*
 * LVGL pauses refreshing while nothing changes. A moving list keeps it going by itself; this
 * restarts it when the glide resumes (after a touch, a page change, or a speed change).
 */
static void kick_tick(lv_timer_t *timer)
{
    page_t *p = active_page();
    if (glide_allowed(p, settings_get_roll_speed())) {
        lv_obj_invalidate(p->list);
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* Widgets                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

static void on_gear_clicked(lv_event_t *e)
{
    ui_settings_open();
}

static void on_row_clicked(lv_event_t *e)
{
    const row_t *row = lv_event_get_user_data(e);
    ui_detail_open(row->id);
}

static void create_header(void)
{
    lv_obj_t *header = ui_box_create(s_screen);
    lv_obj_set_size(header, 480, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    /* Top row (title, settings, status) centred on y=17; the rolling banner fills the bottom row */
    s_title = ui_label_create(header, &font_24, UI_FLAT);
    lv_label_set_text(s_title, "Stonks?");
    lv_obj_set_pos(s_title, 16, 2);
    s_title_icon = lv_image_create(header);
    lv_obj_add_flag(s_title_icon, LV_OBJ_FLAG_HIDDEN);
    ui_banner_create(header, 16, 33, 480 - 2 * 16);

    /* Gear: opens the Settings screen. Transparent 32x32 button with a larger touch area */
    lv_obj_t *gear = lv_button_create(header);
    lv_obj_set_size(gear, 32, 32);
    lv_obj_set_pos(gear, GEAR_X, 1);
    lv_obj_set_style_bg_opa(gear, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_set_ext_click_area(gear, 8);
    lv_obj_add_event_cb(gear, on_gear_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gear_icon = ui_label_create(gear, &font_24, UI_TEXT_SECONDARY);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_center(gear_icon);

    /* Wi-Fi state icon at a fixed spot, with a red slash drawn over it when there is no Wi-Fi */
    s_wifi_icon = ui_label_create(header, &font_14, UI_DOWN);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_pos(s_wifi_icon, WIFI_ICON_X, 8);

    static const lv_point_precise_t SLASH[] = {{0, 17}, {20, 0}};
    s_wifi_slash = lv_line_create(header);
    lv_line_set_points(s_wifi_slash, SLASH, 2);
    lv_obj_set_pos(s_wifi_slash, WIFI_ICON_X - 1, 8);
    lv_obj_set_style_line_width(s_wifi_slash, 2, 0);
    lv_obj_set_style_line_color(s_wifi_slash, UI_DOWN, 0);
    lv_obj_set_style_line_rounded(s_wifi_slash, true, 0);

    s_update_time = ui_label_create(header, &font_14, UI_TEXT_SECONDARY);
    lv_label_set_text(s_update_time, "--");
    lv_obj_align(s_update_time, LV_ALIGN_TOP_RIGHT, -16, 8);
}

static void create_captions(void)
{
    lv_obj_t *bar = ui_box_create(s_screen);
    lv_obj_set_size(bar, 480, CAPTIONS_H);
    lv_obj_set_pos(bar, 0, UI_HEADER_H);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, UI_DIVIDER, 0);

    static const struct {
        const char *text;
        lv_align_t align;
        int32_t x;
    } CAPTIONS[] = {
        {"Symbol", LV_ALIGN_LEFT_MID, 16},
        {"Last", LV_ALIGN_RIGHT_MID, -(480 - MID_RIGHT)},
        {"Chg %", LV_ALIGN_RIGHT_MID, -16},
    };
    for (size_t i = 0; i < sizeof(CAPTIONS) / sizeof(CAPTIONS[0]); i++) {
        lv_obj_t *label = ui_label_create(bar, &font_14, UI_TEXT_SECONDARY);
        lv_label_set_text(label, CAPTIONS[i].text);
        lv_obj_align(label, CAPTIONS[i].align, CAPTIONS[i].x, 0);
    }

    s_page_label = ui_label_create(bar, &font_14, UI_TEXT_PRIMARY);
    lv_label_set_text(s_page_label, "Stocks 1/2");
    lv_obj_align(s_page_label, LV_ALIGN_CENTER, 0, 0);
}

static void on_page_changed(lv_event_t *e)
{
    ui_set_text(s_page_label, active_page() == &s_stocks ? "Stocks 1/2" : "Market 2/2");
}

/* A page in the tileview, with its list of rows: scrolls vertically and snaps to a row */
static void create_page(page_t *p, uint8_t col, lv_dir_t swipe_dir)
{
    p->tile = lv_tileview_add_tile(s_tileview, col, 0, swipe_dir);
    p->list = ui_box_create(p->tile);
    lv_obj_set_size(p->list, 480, LIST_H);
    lv_obj_add_flag(p->list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(p->list, LV_DIR_VER); /* sideways drags go to the tileview */
    lv_obj_set_scrollbar_mode(p->list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(p->list, LV_SCROLL_SNAP_START); /* manual scrolling stops on a row */
    lv_obj_set_flex_flow(p->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(p->list, on_list_scroll, LV_EVENT_SCROLL, p);
    lv_obj_add_event_cb(p->list, on_list_scroll, LV_EVENT_SCROLL_END, p);
    lv_obj_add_event_cb(p->list, on_list_touch, LV_EVENT_PRESSED, p);
    lv_obj_add_event_cb(p->list, on_list_touch, LV_EVENT_PRESSING, p);
    lv_obj_add_event_cb(p->list, on_list_touch, LV_EVENT_RELEASED, p);
    lv_obj_add_event_cb(p->list, on_list_touch, LV_EVENT_PRESS_LOST, p);
}

/* Gradient strip over a list edge. The top one fades from the background into the list. */
static void create_fade(int32_t y, int32_t h, bool top)
{
    lv_obj_t *fade = ui_box_create(s_screen); /* not clickable: touches go to the list */
    lv_obj_set_size(fade, 480, h);
    lv_obj_set_pos(fade, 0, y);
    lv_obj_set_style_bg_color(fade, UI_BG_SCREEN, 0);
    lv_obj_set_style_bg_grad_color(fade, UI_BG_SCREEN, 0);
    lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(fade, top ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(fade, top ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(fade, LV_OPA_COVER, 0);
}

static void create_row(row_t *r, lv_obj_t *list)
{
    r->row = ui_box_create(list);
    lv_obj_set_size(r->row, 480, ROW_H);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->row, UI_BG_SCREEN, 0);
    lv_obj_set_style_bg_color(r->row, UI_BG_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_side(r->row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r->row, 1, 0);
    lv_obj_set_style_border_color(r->row, UI_DIVIDER, 0);
    /* Clickable but not scrollable: a drag on a row scrolls the list, a tap opens the detail */
    lv_obj_add_flag(r->row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE); /* touches reach the list too */
    lv_obj_add_event_cb(r->row, on_row_clicked, LV_EVENT_CLICKED, r);

    r->symbol = ui_label_create(r->row, &font_24, UI_GOLD);
    lv_obj_set_pos(r->symbol, 16, 10);

    r->badge = lv_image_create(r->row);
    lv_obj_add_flag(r->badge, LV_OBJ_FLAG_HIDDEN);

    /* Up to two lines, then "..." */
    r->name = ui_label_create(r->row, &font_14, UI_TEXT_SECONDARY);
    lv_obj_set_pos(r->name, 16, 42);
    ui_label_max_lines(r->name, NAME_W, 2);

    r->last = ui_label_create(r->row, &font_24, UI_TEXT_PRIMARY);
    lv_obj_align(r->last, LV_ALIGN_TOP_RIGHT, -(480 - MID_RIGHT), 10);

    r->change = ui_label_create(r->row, &font_16, UI_FLAT);
    lv_obj_align(r->change, LV_ALIGN_TOP_RIGHT, -(480 - MID_RIGHT), 46);

    r->pill = ui_box_create(r->row);
    lv_obj_set_size(r->pill, 112, 40);
    lv_obj_align(r->pill, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_radius(r->pill, 8, 0);
    lv_obj_set_style_bg_opa(r->pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->pill, UI_FLAT, 0);

    /* Black text: better contrast than white on the green and red fills */
    r->pct = ui_label_create(r->pill, &font_20, lv_color_black());
    lv_obj_center(r->pct);
}

static void update_row(row_t *r, const stock_t *s)
{
    char buf[STOCK_NAME_LEN];
    strlcpy(r->id, s->symbol, sizeof(r->id));
    ui_set_text(r->symbol, s->ticker);
    lv_obj_set_style_text_color(r->symbol, ui_ticker_color(s), 0);
    ui_set_text(r->name, s->not_found ? "No data (delisted?)" : s->name);

    ui_fmt_price(buf, sizeof(buf), s->last, s->has_quote, s->format);
    ui_set_text(r->last, buf);
    ui_fmt_change(buf, sizeof(buf), s->change, s->has_quote, s->format);
    ui_set_text(r->change, buf);
    ui_fmt_pct(buf, sizeof(buf), s->change_pct, s->has_quote);
    ui_set_text(r->pct, s->not_found ? "n/a" : buf);

    /* By the % change: it decides the colour the same way for every format */
    lv_color_t color = ui_trend_color(s->change_pct, s->has_quote);
    lv_obj_set_style_text_color(r->change, color, 0);
    lv_obj_set_style_bg_color(r->pill, color, 0);
}

/* Best performer: money bag (above MONEYBAG_PCT) or top hat (above TOPHAT_PCT).
   Worst performer: flying money (below LOSER_PCT). */
static void update_badges(const stock_t *list, int n)
{
    int best = -1;
    int worst = -1;
    for (int i = 0; i < n; i++) {
        if (!list[i].has_quote || list[i].not_found) {
            continue;
        }
        if (best < 0 || list[i].change_pct > list[best].change_pct) {
            best = i;
        }
        if (worst < 0 || list[i].change_pct < list[worst].change_pct) {
            worst = i;
        }
    }
    for (int i = 0; i < s_stocks.row_count && i < n; i++) {
        row_t *r = &s_stocks.rows[i];
        const lv_image_dsc_t *img = NULL;
        if (i == best && list[i].change_pct > TOPHAT_PCT) {
            img = &img_tophat;
        } else if (i == best && list[i].change_pct > MONEYBAG_PCT) {
            img = &img_moneybag;
        } else if (i == worst && list[i].change_pct < LOSER_PCT) {
            img = &img_money_wings;
        }
        if (img == NULL) {
            lv_obj_add_flag(r->badge, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        /* Just right of the ticker text, vertically centred on its line (y 10..40) */
        lv_point_t size;
        lv_text_get_size(&size, lv_label_get_text(r->symbol), &font_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_image_set_src(r->badge, img);
        lv_obj_set_pos(r->badge, 16 + size.x + 8, 9);
        lv_obj_remove_flag(r->badge, LV_OBJ_FLAG_HIDDEN);
    }
}

/* "Stonks!" when the stocks are up on average, "Not stonks..." when down, and
   "CIRCUIT BREAKER!" when the Ibovespa has fallen far enough for B3 to halt trading */
static void update_title(const stock_t *list, int n)
{
    double sum = 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (list[i].has_quote && !list[i].not_found) {
            sum += list[i].change_pct;
            count++;
        }
    }
    double average = count ? sum / count : 0;
    const char *text = "Stonks?";
    lv_color_t color = UI_FLAT;
    const lv_image_dsc_t *icon = NULL;
    bool circuit_breaker = model_circuit_breaker();
    ui_banner_set_alert(circuit_breaker);
    if (circuit_breaker) {
        text = "CIRCUIT BREAKER!";
        color = UI_DOWN;
        icon = &img_lightning;
    } else if (count > 0 && average >= 0.005) {
        text = "Stonks!";
        color = UI_UP;
        icon = &img_chart_up;
    } else if (count > 0 && average <= -0.005) {
        text = "Not stonks...";
        color = UI_DOWN;
        icon = &img_chart_down;
    }
    ui_set_text(s_title, text);
    lv_obj_set_style_text_color(s_title, color, 0);

    if (icon == NULL) {
        lv_obj_add_flag(s_title_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    /* Just right of the title text, vertically centred on its line (y 2..32) */
    lv_point_t size;
    lv_text_get_size(&size, text, &font_24, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_image_set_src(s_title_icon, icon);
    lv_obj_set_pos(s_title_icon, 16 + size.x + 8, 1);
    lv_obj_remove_flag(s_title_icon, LV_OBJ_FLAG_HIDDEN);
}

static void update_status(void)
{
    app_status_t st;
    model_get_status(&st);
    char time_buf[12];

    bool wifi = st.net == NET_ONLINE;
    bool internet = wifi && st.last_update != 0 && st.error[0] == '\0'; /* last refresh worked */
    lv_obj_set_style_text_color(s_wifi_icon, !wifi ? UI_DOWN : (internet ? UI_UP : UI_GOLD), 0);
    if (wifi) {
        lv_obj_add_flag(s_wifi_slash, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_wifi_slash, LV_OBJ_FLAG_HIDDEN);
    }

    ui_fmt_time(time_buf, sizeof(time_buf), st.last_update, false);
    ui_set_text(s_update_time, time_buf);
}

lv_obj_t *ui_watchlist_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, UI_BG_SCREEN, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header();
    create_captions();

    /* Two pages side by side: swipe left for the market, right to come back */
    s_tileview = lv_tileview_create(s_screen);
    lv_obj_set_size(s_tileview, 480, LIST_H);
    lv_obj_set_pos(s_tileview, 0, LIST_Y);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, on_page_changed, LV_EVENT_VALUE_CHANGED, NULL);
    create_page(&s_stocks, 0, LV_DIR_RIGHT);
    create_page(&s_market, 1, LV_DIR_LEFT);
    lv_tileview_set_tile(s_tileview, s_stocks.tile, LV_ANIM_OFF);

    create_fade(LIST_Y, FADE_TOP_H, true);
    create_fade(480 - FADE_BOTTOM_H, FADE_BOTTOM_H, false);

    lv_display_add_event_cb(lv_display_get_default(), on_refresh_start, LV_EVENT_REFR_START, NULL);
    lv_timer_create(kick_tick, KICK_TICK_MS, NULL);
    return s_screen;
}

lv_obj_t *ui_watchlist_screen(void)
{
    return s_screen;
}

/* (Re)creates a page's rows, one per stock */
static void build_page(page_t *p, int n)
{
    lv_obj_clean(p->list);
    for (int i = 0; i < n; i++) {
        create_row(&p->rows[i], p->list);
    }
    p->row_count = n;
    /* Loop only when the rows more than fill the view, with a spare row at each end */
    p->wheel = n * ROW_H >= LIST_H + 2 * ROW_H;
    /* Otherwise leave room to scroll the last row clear of the bottom fade */
    lv_obj_set_style_pad_bottom(p->list, p->wheel ? 0 : FADE_BOTTOM_H, 0);
    lv_obj_update_layout(p->list);
    if (p->wheel) {
        /* The last row goes above the first, scrolled out of view: the list starts at row 1 */
        lv_obj_move_to_index(lv_obj_get_child(p->list, -1), 0);
        lv_obj_update_layout(p->list);
        lv_obj_scroll_to_y(p->list, ROW_H, LV_ANIM_OFF);
    }
}

void ui_watchlist_update(void)
{
    uint32_t data_version = model_data_version();
    if (data_version == s_data_version) {
        return;
    }
    s_data_version = data_version;

    uint32_t list_version = model_list_version();
    int total = model_snapshot(s_snapshot, STOCK_MAX);
    /* The snapshot has the watchlist first, then the market rows */
    int n = 0;
    while (n < total && s_snapshot[n].group == STOCK_GROUP_WATCHLIST) {
        n++;
    }
    const stock_t *market = &s_snapshot[n];
    int market_n = total - n;

    if (list_version != s_list_version) {
        s_list_version = list_version;
        build_page(&s_stocks, n);
        build_page(&s_market, market_n);
    }
    for (int i = 0; i < s_stocks.row_count && i < n; i++) {
        update_row(&s_stocks.rows[i], &s_snapshot[i]);
    }
    for (int i = 0; i < s_market.row_count && i < market_n; i++) {
        update_row(&s_market.rows[i], &market[i]);
    }
    update_badges(s_snapshot, n);
    update_title(s_snapshot, n);
    update_status();
}
