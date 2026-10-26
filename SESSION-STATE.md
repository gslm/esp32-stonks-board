# Session state

Living checkpoint of in-flight work (see DIRECTIVES.md, rule 6). Keep it short and current.
Last updated: 2026-10-26.

## Done

- Repo created and pushed: https://github.com/gslm/esp32-stonks-board (branch `main`).
- No absolute paths in tracked files. `.vscode/settings.json` is untracked; copy it from
  `.vscode/settings.example.json`.
- Display: 19.5 FPS while the list glides (was 11). It renders on both cores, uses anti-tearing
  direct mode with 2 frame buffers, and moves the glide once per frame. All of it is in
  sdkconfig.defaults or the code.
- Tried and dropped: double draw buffers (not possible with esp_lvgl_port's synchronous copy),
  a 480-line draw buffer (slower than direct mode), PSRAM at 120 MHz (needs 120 MHz flash too;
  experimental, not pursued).

- Settings screen, Wi-Fi profiles and scan (pushed, 2394fb2). Open: Home2 "homeTest" fails with
  reason 201 (not found), likely weak signal; the operator's scan result is still pending.
- 2026-10-26, checked by the operator, committed and pushed:
  - Market page: swipe between "Stocks 1/2" and "Market 2/2". Purple tickers: IBOV (^BVSP),
    USD/BRL (BRL=X), EWZ (Yahoo), DI11!, DI1F28/29/30 (TradingView public scanner, % a year).
    Mood title and badges use the stocks only.
  - Settings > Scroll Options: the speed slider (moved from the main header, both pages) and one
    auto-scroll checkbox per page (NVS autoscroll0/1, default on). Both pages are endless wheels.
  - Settings > Screen: brightness 10..100 % (NVS brightness) and "Upside down (180°)" (NVS
    orientation; the default is the panel turned 180°, ticked = the panel's own orientation).
    The ST7701 flips itself (SDIR SS + MADCTL ML over a re-created 3-wire SPI IO). 90° dropped.
  - Touch X correction: the GT911 stretches X near the side edges (measured with crosshair
    taps); X is squeezed into 20..459 (TOUCH_X_MARGIN in screen.c). Y is accurate.

- Circuit breaker mode (2026-10-26, checked by the operator with `cbtest on`): while IBOV is at
  -10 % or lower, red "CIRCUIT BREAKER!" title + lightning emoji, red banner with quotes from
  `circuit_breaker_quotes.txt`. Commit commands given to the operator, who commits and pushes.

- Docs brought up to date with the firmware (2026-10-26, operator cleared it; not committed
  yet, commit commands given to the operator): ui-spec revision 4 (header with gear, Wi-Fi
  icon, update time and banner; Market page; 92 px rows; badges; Settings screen), README
  (repo name, Docs table, code map), lvgl-pro-guide and lvgl_pro_ui/README (design is at
  revision 3, full list of known differences), research-notes status note.
  Left alone (code/config comments, operator to decide): "watchlist in NVS" in
  partitions.csv and sdkconfig.defaults, `CONFIG_STOCK_WIFI_*` in .gitignore,
  `01_hello_counter` in main/idf_component.yml and sdkconfig.defaults, `02_stock_watchlist`
  in lvgl_pro_ui/project.xml, "Updated hh:mm:ss" in wifi.c.

## In progress: learning guide `docs/firmware-guide.md` (operator asked 2026-10-26)

In depth, for the operator to learn ESP-IDF and LVGL from this code. One section per checkpoint:
1. Architecture decisions: written (16 subsections), verified against the sources.
2. Hardware platform, 3. Components, 4. Files down to functions, 5. Other: to do, one at a time.

Note: the console input only works on the USB TO UART port; the USB-OTG port is output only.

## Next (operator to decide)

- JTAG debugging over the USB-OTG port.
- Moving the firmware UI to LVGL Pro exported code (needs `CONFIG_LV_USE_OBJ_NAME=y`, and
  removing `main/fonts/`; see docs/lvgl-pro-guide.md). The design project is behind the
  firmware: it lacks the banner, the bigger rows and the emoji badges.
