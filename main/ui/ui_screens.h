/* Functions the screens call on each other. All run in the LVGL task. */
#pragma once

#include "lvgl.h"

lv_obj_t *ui_watchlist_create(void);
void ui_watchlist_update(void);
lv_obj_t *ui_watchlist_screen(void);

/* Rolling message strip under the title (ui_banner.c) */
void ui_banner_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width);

void ui_detail_create(void);
void ui_detail_open(const char *symbol);
void ui_detail_update(void);


/* lv_label_set_text, skipped when the text is unchanged (avoids needless redraws) */
void ui_set_text(lv_obj_t *label, const char *text);
