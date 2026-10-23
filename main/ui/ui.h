/*
 * The LVGL user interface. Everything here runs in the LVGL task: ui_init() is called with the
 * display lock held, and a 250 ms LVGL timer pulls new data from the model. No other task calls
 * into the UI, so nothing here needs to take the lock again.
 */
#pragma once

void ui_init(void);
