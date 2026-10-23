/*
 * Main screen: a mood title ("Stonks!" / "Not stonks..."), a roll-speed slider, column captions,
 * and the stocks as an endless wheel that glides on its own (or scrolls by hand, still endlessly).
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
 * Auto-roll speed comes from the header slider (also the "speed" console command), in tenths:
 * 0 = stopped .. 10 = 1.0x, which moves one row per second. The list glides continuously.
 */
#define KICK_TICK_MS    100
#define TOUCH_RESUME_MS 5000 /* auto-roll waits this long after the list was last touched */
#define SLIDER_X        216 /* leaves room for "Not stonks..." plus its chart icon */
#define SLIDER_W        116

typedef struct {
    lv_obj_t *row;
    lv_obj_t *symbol;
    lv_obj_t *badge; /* money bag or top hat (best performer), flying money (worst), else hidden */
    lv_obj_t *name;
    lv_obj_t *last;
    lv_obj_t *change;
    lv_obj_t *pill;
    lv_obj_t *pct;
    char id[STOCK_SYMBOL_LEN]; /* Yahoo symbol the row shows */
} row_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_title_icon; /* chart up or down, after the title */
static lv_obj_t *s_status;
static lv_obj_t *s_speed_slider;
static lv_obj_t *s_speed_label;
static lv_obj_t *s_list;
static row_t s_rows[STOCK_MAX];
static int s_row_count;
static uint32_t s_list_version = UINT32_MAX;
static uint32_t s_data_version = UINT32_MAX;
EXT_RAM_BSS_ATTR static stock_t s_snapshot[STOCK_MAX];

static bool s_wheel;              /* enough rows to loop endlessly */
static bool s_list_pressed;       /* a finger is on the list right now */
static uint32_t s_list_touch_ms;  /* when the list was last touched */
static uint32_t s_glide_last_ms;
static int32_t s_glide_rem;       /* sub-pixel remainder of the glide, in 1/1000 px */

/* ---------------------------------------------------------------------------------------------- */
/* Endless wheel                                                                                   */
/* ---------------------------------------------------------------------------------------------- */

/* Move rows between the ends until the scroll position is back in [ROW_H, 2 * ROW_H) */
static void wheel_normalize(void)
{
    static bool busy;
    /* LVGL's own scroll animations (snap to row) aim at an absolute position: let them finish */
    if (!s_wheel || busy || lv_anim_get(s_list, NULL) != NULL) {
        return;
    }
    busy = true;
    int32_t y = lv_obj_get_scroll_y(s_list);
    int32_t start = y;
    while (y >= 2 * ROW_H) {
        lv_obj_move_to_index(lv_obj_get_child(s_list, 0), -1); /* top row -> bottom */
        y -= ROW_H;
    }
    while (y < ROW_H) {
        lv_obj_move_to_index(lv_obj_get_child(s_list, -1), 0); /* bottom row -> top */
        y += ROW_H;
    }
    if (y != start) {
        lv_obj_update_layout(s_list);
        lv_obj_scroll_to_y(s_list, y, LV_ANIM_OFF);
    }
    busy = false;
}

static void on_list_scroll(lv_event_t *e)
{
    wheel_normalize();
}

/* Touches on the list (rows bubble their events up to it) pause the auto-roll; the slider doesn't */
static void on_list_touch(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    s_list_pressed = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING;
    s_list_touch_ms = lv_tick_get();
}

static void show_speed(int tenths)
{
    char buf[16];
    if (tenths == 0) {
        snprintf(buf, sizeof(buf), "Stop");
    } else {
        snprintf(buf, sizeof(buf), "%d.%dx", tenths / 10, tenths % 10);
    }
    ui_set_text(s_speed_label, buf);
}

static void on_speed_changed(lv_event_t *e)
{
    int tenths = lv_slider_get_value(s_speed_slider);
    /* Apply while dragging, but write flash only once, when the finger lifts */
    settings_set_roll_speed(tenths, lv_event_get_code(e) == LV_EVENT_RELEASED);
    show_speed(tenths);
}

static bool glide_allowed(int tenths)
{
    return tenths > 0 && s_wheel && lv_screen_active() == s_screen && !s_list_pressed &&
           lv_tick_diff(lv_tick_get(), s_list_touch_ms) >= TOUCH_RESUME_MS && lv_anim_get(s_list, NULL) == NULL;
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

    int tenths = settings_get_roll_speed();
    if (!glide_allowed(tenths)) {
        s_glide_rem = 0;
        return;
    }
    /* At 1.0x the list moves ROW_H px per second: that is ROW_H * tenths / 10 thousandths of a px per ms */
    int32_t milli_px = (int32_t)(elapsed * ROW_H * (uint32_t)tenths / 10) + s_glide_rem;
    s_glide_rem = milli_px % 1000;
    if (milli_px >= 1000) {
        lv_obj_scroll_by(s_list, 0, -(milli_px / 1000), LV_ANIM_OFF); /* negative dy = content moves up */
    }
}

/*
 * LVGL pauses refreshing while nothing changes. A moving list keeps it going by itself; this
 * restarts it when the glide resumes (after a touch, or a speed change from the console), and
 * keeps the slider in step with the console's "speed" command.
 */
static void kick_tick(lv_timer_t *timer)
{
    int tenths = settings_get_roll_speed();
    if (!lv_obj_has_state(s_speed_slider, LV_STATE_PRESSED) && lv_slider_get_value(s_speed_slider) != tenths) {
        lv_slider_set_value(s_speed_slider, tenths, LV_ANIM_OFF);
        show_speed(tenths);
    }
    if (glide_allowed(tenths)) {
        lv_obj_invalidate(s_list);
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* Widgets                                                                                         */
/* ---------------------------------------------------------------------------------------------- */

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

    /* Top row (title, slider, status) centred on y=17; the rolling banner fills the bottom row */
    s_title = ui_label_create(header, &font_24, UI_FLAT);
    lv_label_set_text(s_title, "Stonks?");
    lv_obj_set_pos(s_title, 16, 2);
    s_title_icon = lv_image_create(header);
    lv_obj_add_flag(s_title_icon, LV_OBJ_FLAG_HIDDEN);
    ui_banner_create(header, 16, 33, 480 - 2 * 16);

    /* Auto-roll speed: 0 (stopped) .. 1.0x */
    s_speed_slider = lv_slider_create(header);
    lv_obj_set_size(s_speed_slider, SLIDER_W, 6);
    lv_obj_set_pos(s_speed_slider, SLIDER_X, 14);
    lv_slider_set_range(s_speed_slider, 0, SETTINGS_ROLL_SPEED_MAX);
    lv_slider_set_value(s_speed_slider, settings_get_roll_speed(), LV_ANIM_OFF);
    lv_obj_set_ext_click_area(s_speed_slider, 18); /* the track is thin: make it easy to grab */
    /* The default theme draws the track at 20 % opacity, invisible on the header: make it solid */
    lv_obj_set_style_bg_color(s_speed_slider, UI_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_speed_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_speed_slider, UI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_speed_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_speed_slider, 6, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s_speed_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(s_speed_slider, on_speed_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_speed_slider, on_speed_changed, LV_EVENT_RELEASED, NULL);

    s_speed_label = ui_label_create(header, &font_14, UI_TEXT_SECONDARY);
    lv_obj_set_pos(s_speed_label, SLIDER_X + SLIDER_W + 14, 8);
    show_speed(settings_get_roll_speed());

    s_status = ui_label_create(header, &font_14, UI_TEXT_SECONDARY);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -16, 8);
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

static void create_row(row_t *r)
{
    r->row = ui_box_create(s_list);
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
    ui_set_text(r->symbol, stock_display_symbol(s->symbol, buf, sizeof(buf)));
    ui_set_text(r->name, s->not_found ? "No data on Yahoo (delisted?)" : s->name);

    ui_fmt_price(buf, sizeof(buf), s->last, s->has_quote);
    ui_set_text(r->last, buf);
    ui_fmt_change(buf, sizeof(buf), s->change, s->has_quote);
    ui_set_text(r->change, buf);
    ui_fmt_pct(buf, sizeof(buf), s->change_pct, s->has_quote);
    ui_set_text(r->pct, s->not_found ? "n/a" : buf);

    lv_color_t color = ui_trend_color(s->change, s->has_quote);
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
    for (int i = 0; i < s_row_count && i < n; i++) {
        row_t *r = &s_rows[i];
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

/* "Stonks!" when the stocks are up on average, "Not stonks..." when down */
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
    if (count > 0 && average >= 0.005) {
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
    char buf[32];
    char time_buf[12];

    if (st.net == NET_NOT_CONFIGURED) {
        ui_set_text(s_status, LV_SYMBOL_WARNING " Wi-Fi");
        lv_obj_set_style_text_color(s_status, UI_DOWN, 0);
    } else if (st.net == NET_CONNECTING) {
        ui_set_text(s_status, LV_SYMBOL_WIFI " ...");
        lv_obj_set_style_text_color(s_status, UI_TEXT_SECONDARY, 0);
    } else if (st.error[0]) {
        ui_set_text(s_status, LV_SYMBOL_WARNING " Error");
        lv_obj_set_style_text_color(s_status, UI_DOWN, 0);
    } else {
        ui_fmt_time(time_buf, sizeof(time_buf), st.last_update, false);
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s", time_buf);
        ui_set_text(s_status, buf);
        lv_obj_set_style_text_color(s_status, UI_TEXT_SECONDARY, 0);
    }
}

lv_obj_t *ui_watchlist_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, UI_BG_SCREEN, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header();
    create_captions();

    s_list = ui_box_create(s_screen);
    lv_obj_set_size(s_list, 480, LIST_H);
    lv_obj_set_pos(s_list, 0, LIST_Y);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(s_list, LV_SCROLL_SNAP_START); /* manual scrolling stops on a row */
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(s_list, on_list_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_list, on_list_scroll, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(s_list, on_list_touch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_list, on_list_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_list, on_list_touch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_list, on_list_touch, LV_EVENT_PRESS_LOST, NULL);

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

void ui_watchlist_update(void)
{
    uint32_t data_version = model_data_version();
    if (data_version == s_data_version) {
        return;
    }
    s_data_version = data_version;

    uint32_t list_version = model_list_version();
    int n = model_snapshot(s_snapshot, STOCK_MAX);
    if (list_version != s_list_version) {
        s_list_version = list_version;
        lv_obj_clean(s_list);
        for (int i = 0; i < n; i++) {
            create_row(&s_rows[i]);
        }
        s_row_count = n;
        /* Loop only when the rows more than fill the view, with a spare row at each end */
        s_wheel = n * ROW_H >= LIST_H + 2 * ROW_H;
        lv_obj_update_layout(s_list);
        if (s_wheel) {
            /* The last row goes above the first, scrolled out of view: the list starts at stock 1 */
            lv_obj_move_to_index(lv_obj_get_child(s_list, -1), 0);
            lv_obj_update_layout(s_list);
            lv_obj_scroll_to_y(s_list, ROW_H, LV_ANIM_OFF);
        }
    }
    for (int i = 0; i < s_row_count && i < n; i++) {
        update_row(&s_rows[i], &s_snapshot[i]);
    }
    update_badges(s_snapshot, n);
    update_title(s_snapshot, n);
    update_status();
}
