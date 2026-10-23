# stock_ui: LVGL Pro design of the stock watchlist

An LVGL Pro Editor 2.0.x project with the same UI as the hand-written firmware in
`main/ui/`. Both follow [`docs/ui-spec.md`](../docs/ui-spec.md).
Target: Waveshare ESP32-S3-Touch-LCD-4B, 480x480, RGB565, LVGL 9.5.0.

New to the editor? Read [`docs/lvgl-pro-guide.md`](../docs/lvgl-pro-guide.md) first.

## What's inside

- `project.xml`: the 480x480 display and `lvgl_version="9.5.0"`
- `globals.xml`: colour and size constants from the spec, the fonts, the shared styles
- `components/`
  - `box`: a plain container without theme styles (like `ui_box_create()`)
  - `stock_row`: one watchlist row (`symbol`, `full_name`, `last`, `change`, `change_pct`, `trend_color`)
  - `app_header`: the watchlist header (`title`, `title_color`, `status`) with the speed slider and its readout
  - `stats_cell`: caption and value, for the detail screen's stats grid
- `screens/`
  - `screen_watchlist`: the 13 default stocks and the list's edge fades
  - `screen_detail`: VALE3 with the intraday chart and the stats
- `fonts/`: Montserrat Medium and Font Awesome 5 Free Solid
- `stock_ui.c/.h`, `user_config.cmake`, `sim/main.c`: user files, kept across exports
- `sim/`, `.vscode/`: the built-in PC simulator (F5), copied from the LVGL Pro tutorials

**Ctrl+B** exports the C code next to the XML files (`*_gen.c/.h`, `CMakeLists.txt`,
`file_list_gen.cmake`, `fonts/*_data.c`). Never edit the generated files.

## Font licences

- `fonts/Montserrat-Medium.ttf`: Copyright 2011 The Montserrat Project Authors,
  SIL Open Font License 1.1 (<https://openfontlicense.org>). Copied from the LVGL Pro examples.
- `fonts/FontAwesome5.ttf`: Font Awesome 5 Free Solid 5.9.0 by Fonticons, Inc., SIL Open Font
  License 1.1. Copied from the LVGL Pro "basic" template. It has the same glyphs as
  `FontAwesome5-Solid+Brands+Regular.woff`, which LVGL uses for its built-in `LV_SYMBOL_*` icons.
