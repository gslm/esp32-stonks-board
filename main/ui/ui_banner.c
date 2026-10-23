/*
 * Rolling banner: one line of text that moves from right to left across a strip along the
 * bottom of the top bar. Each time a quote has scrolled off, the next one is picked at random
 * from money_quotes.txt (see banner_messages.c).
 */
#include "banner_messages.h"
#include "esp_log.h"
#include "esp_random.h"
#include "ui_screens.h"
#include "ui_theme.h"

#define BANNER_SPEED_PX_S 40 /* fixed speed; tune while testing */
#define QUOTE_MAX_LEN     512

static lv_obj_t *s_strip;
static lv_obj_t *s_label;
static int s_last_message = -1;

static void set_x(void *label, int32_t x)
{
    lv_obj_set_x(label, x);
}

static void show_next_quote(void);

static void on_scrolled_off(lv_anim_t *a)
{
    show_next_quote();
}

/* Enters at the right edge and runs until it has left at the left edge */
static void start_scroll(void)
{
    lv_anim_delete(s_label, set_x);
    lv_obj_update_layout(s_label);
    int32_t text_w = lv_obj_get_width(s_label);
    int32_t strip_w = lv_obj_get_width(s_strip);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_label);
    lv_anim_set_exec_cb(&a, set_x);
    lv_anim_set_values(&a, strip_w, -text_w);
    lv_anim_set_duration(&a, (uint32_t)(text_w + strip_w) * 1000 / BANNER_SPEED_PX_S);
    lv_anim_set_completed_cb(&a, on_scrolled_off);
    lv_anim_start(&a);
}

static void show_next_quote(void)
{
    static char quote[QUOTE_MAX_LEN];
    int count = banner_message_count();
    if (count == 0) {
        return;
    }
    int i = (int)(esp_random() % (uint32_t)count);
    if (count > 1 && i == s_last_message) {
        i = (i + 1) % count; /* never the same quote twice in a row */
    }
    s_last_message = i;
    banner_message_get(i, quote, sizeof(quote));
    lv_label_set_text(s_label, quote);
    start_scroll();
}

void ui_banner_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width)
{
    /* The strip clips the label, so the text appears and disappears at its edges */
    s_strip = ui_box_create(parent);
    lv_obj_set_pos(s_strip, x, y);
    lv_obj_set_size(s_strip, width, lv_font_get_line_height(&font_14));

    s_label = ui_label_create(s_strip, &font_14, UI_GOLD); /* same colour as the tickers; one line, as wide as its text */
    ESP_LOGI("banner", "%d quotes from money_quotes.txt", banner_message_count());
    show_next_quote();
}
