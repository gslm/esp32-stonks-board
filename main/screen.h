/*
 * Display hardware settings: backlight brightness, and turning the picture upside down (180°).
 * The default picture is the panel's own orientation turned 180°; "upside down" is the panel's
 * own orientation. Call from the LVGL task (or with the display lock held). Saving is up to the
 * caller (settings.c).
 */
#pragma once

#include <stdbool.h>

/* Applies the saved brightness and orientation. Call once, after bsp_display_start(). */
void screen_init(void);

void screen_set_brightness(int percent);
void screen_set_upside_down(bool on);
