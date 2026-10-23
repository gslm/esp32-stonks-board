#include <string.h>
#include "ui.h"
#include "ui_screens.h"
#include "ui_theme.h"

#define UI_POLL_MS 250

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

/* Runs in the LVGL task: pulls whatever changed in the model into the widgets */
static void ui_tick(lv_timer_t *timer)
{
    ui_watchlist_update();
    ui_detail_update();
}

void ui_init(void)
{
    ui_theme_init(lv_display_get_default());
    ui_watchlist_create();
    ui_detail_create();
    lv_screen_load(ui_watchlist_screen());
    ui_tick(NULL);
    lv_timer_create(ui_tick, UI_POLL_MS, NULL);
}
