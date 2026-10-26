# 02_stock_watchlist

Shows the Investing.com "Ativos" watchlist (13 B3 tickers) on the 480x480 screen, with prices from
Yahoo Finance refreshed every 30 s, plus a second page of market indicators.

- **Two pages:** swipe left or right between "Stocks 1/2" (the watchlist, gold tickers) and
  "Market 2/2" (purple tickers). The page name is in the middle of the column captions.
  - Market: Ibovespa (IBOV), the dollar (USD/BRL), the EWZ ETF, and the DI rate futures DI11!
    (nearest contract), DI1F28, DI1F29 and DI1F30, shown as % a year.
- **Each page is an endless wheel** that glides continuously and loops forever.
  - Settings > Scroll Options sets the speed for both pages, from stopped to 1.0x (one row per
    second), default 0.3x, and turns auto-scroll on or off per page. Saved; the `speed` command
    sets the speed too.
  - Drag a list to scroll by hand; it also loops endlessly and snaps to a row.
  - Touching a list pauses its glide, which resumes 5 s later.
- **The title shows the mood.** It reads "Stonks!" in green when the average change % of the stocks is positive, and "Not stonks..." in red when it's negative. The Market page doesn't count.
- **The title gets a chart icon:** up for "Stonks!", down for "Not stonks...".
- **Badges after the ticker:** the day's best performer gets a money bag above +2%, or a top hat above +10%. The worst performer gets flying money below -2%. The thresholds are `MONEYBAG_PCT`, `TOPHAT_PCT` and `LOSER_PCT` in `main/ui/ui_watchlist.c`.
- **A gold banner** along the bottom of the top bar scrolls a random quote from `money_quotes.txt` (one per line). A new quote is picked each time one has scrolled off. The file is built into the firmware, so edit it and rebuild.
- **Tap a row** to see the day range, volume and an intraday chart (no chart for the DI futures).
- **The Wi-Fi icon** (top right) is green when online, yellow when on Wi-Fi without internet (the last refresh failed), and red with a slash when not connected.
- **The gear button** (top right) opens Settings:
  - Wi-Fi: the connection, plus four Wi-Fi profiles (Work, Home2, Home3, Hotspot - Test): tap one to connect, or Edit to change its network and password.
  - Scroll Options: see above.
  - Screen: brightness (10 to 100 %), and "Upside down (180°)". The default orientation is the
    panel's own one turned 180°; ticking the box gives the panel's own orientation. Both are saved.

The watchlist and the market rows are fixed in `main/stock_model.c` (`DEFAULT_STOCKS`, `MARKET`); edit them there and rebuild.

The layout follows [docs/ui-spec.md](docs/ui-spec.md).

## Set up a new workstation

Tested on Ubuntu Linux with the versions below.

1. **ESP-IDF v6.0.1.** Install it with the Espressif Installation Manager (EIM). It installs
   to `~/.espressif/`: IDF in `~/.espressif/v6.0.1/esp-idf`, tools (compiler, esp-clang,
   OpenOCD) in `~/.espressif/tools`.
2. **VSCode extensions:**
   - "ESP-IDF" by Espressif (2.3.0 here). Select ESP-IDF v6.0.1 when it asks.
   - A clangd extension, for Ctrl+click navigation. This machine uses "STM32Cube clangd"; the
     official "clangd" extension also works, because `.vscode/settings.json` sets both.
3. **Clone and build:**

   ```bash
   # new workstation
   cd ~
   git clone <repo-url> 02_stock_watchlist
   cd ~/02_stock_watchlist
   source ~/.espressif/tools/activate_idf_v6.0.1.sh
   idf.py set-target esp32s3
   idf.py build
   ```

   The first build downloads the managed components (BSP, LVGL 9.5.0, cJSON) at the
   versions pinned in `dependencies.lock`.
4. **VSCode settings.** Create the local settings file from the tracked example:

   ```bash
   # new workstation
   cd ~/02_stock_watchlist
   cp .vscode/settings.example.json .vscode/settings.json
   ```

   The example has no absolute paths; clangd paths use `${userHome}` and `${workspaceFolder}`.
   `.vscode/settings.json` is not tracked, because the ESP-IDF extension writes machine-specific
   entries into it (the selected ESP-IDF path, the serial port).
5. **Wi-Fi profile defaults.** The passwords are not in the repo (it is public). Create the
   local, git-ignored file from the template and fill in the four profiles:

   ```bash
   # new workstation
   cd ~/02_stock_watchlist
   cp main/wifi_profiles_local.example.h main/wifi_profiles_local.h
   ```

   Without it the firmware still builds, but the profiles start empty; you can then set them
   on the device (Settings > Wi-Fi > Edit).
6. **Optional, Node.js:** only needed to regenerate `main/fonts/` with `tools/gen_fonts.sh`.
   The generated fonts are committed.
7. **Optional, LVGL Pro Editor 2.0.5** (UI design project in `lvgl_pro_ui/`):
   - Download the Linux AppImage from lvgl.io
     ([installation guide](https://lvgl.io/docs/pro/editor/installation)). The free Community
     tier needs an LVGL account and is for non-commercial use.
   - If it fails to start, run it from a terminal with `--gtk-version=3`.
   - Open the `lvgl_pro_ui/` folder (File > Open Folder). Export (Ctrl+B) needs `git` and
     `make` installed.
   - First steps and how the design maps to the firmware: `docs/lvgl-pro-guide.md`.

## Open it in VSCode

Use **File > Open Folder** and pick this folder. It is an ESP-IDF project at its root, so the
ESP-IDF extension's commands (Ctrl+E D: build, flash, monitor) work directly.

## Wi-Fi profiles

Settings (gear button) > Wi-Fi has four profiles: Work, Home2, Home3 and Hotspot - Test.

- **Connect:** tap a profile. It becomes the active one (marked and outlined), and the choice
  is saved.
- **Edit:** change a profile's SSID and password with the on-screen keyboard. The password
  shows as dots. Leave it empty to keep the current one.
- **Scan networks:** lists the networks the board can see, strongest first, with signal (dBm) and
  channel. Networks used by a profile are highlighted. Around -80 dBm or weaker, connections are
  unreliable. The list is also printed to the serial log.
- **Defaults:** each profile starts from `main/wifi_profiles_local.h` until it is edited on the
  device. Edits are saved in the board's NVS and survive reflashing.
- **After a full erase** (`idf.py erase-flash`): the board starts on Work with the defaults.

## Command line

The `stock>` prompt runs on the **USB TO UART** port. The USB-OTG port shows the logs but takes no input, so typing there does nothing.

| Command | What it does |
| --- | --- |
| `list` | Watchlist and market rows with last price, change and change % |
| `wifi` / `wifi "ssid" "pass"` | Show the Wi-Fi status / set new credentials for the active profile |
| `interval 15` | Refresh every 15 s (10..3600) |
| `speed 0.5` | Glide speed, 0 (stopped) to 1.0 (one row per second). The slider in Settings > Scroll Options follows it. Saved |
| `refresh` | Fetch now |
| `status` | Network, last update, errors, free memory |

## Data source and limits

- **Source:** Yahoo Finance's public chart endpoints. They need no account or API key.
  - Every refresh, one request gets the last price and previous close for all symbols, about 3 KB.
  - Per-symbol requests, round-robin and when a detail screen opens, get open/high/low/volume and the 15-minute intraday series.
- **DI rate futures:** Yahoo doesn't have them. They come from TradingView's public scanner
  endpoint (no account or key), one request per refresh, also about 15 minutes delayed.
- **Delay:** B3 prices on Yahoo are about **15 minutes delayed**. Investing.com's free pages are delayed the same way (the red clock icon), and free real-time B3 data doesn't exist. Refreshing more often than every 30 s only picks up the delayed feed sooner.
- **Terms:** neither Yahoo's nor TradingView's terms cover automated access. Fine for a personal demo; for anything more, switch to a licensed API (see [docs/research-notes.md](docs/research-notes.md)).

## Code map

| File | Role |
| --- | --- |
| `main/main.c` | Startup order: NVS, model, display + UI, Wi-Fi, quote fetcher, console |
| `main/stock_model.c` | The watchlist (`DEFAULT_STOCKS`) and its quotes, shared by all tasks behind one mutex |
| `main/quotes.c` | The only task that talks to the network: HTTPS (`esp_http_client` + certificate bundle) and JSON parsing (cJSON) |
| `main/wifi.c` | Station mode, reconnect every 5 s, SNTP clock (Brasília time) |
| `main/screen.c` | Brightness, the 180° turn (sent to the ST7701 panel itself, so drawing costs nothing), and touch correction: the touch chip stretches X towards the side edges, so X is squeezed into 20..459 |
| `main/settings.c` | Wi-Fi profiles, refresh interval, glide speed, auto-scroll per page, brightness and orientation in NVS. Profile defaults come from `main/wifi_profiles_local.h` (git-ignored; template `wifi_profiles_local.example.h`) |
| `main/cli.c` | The `stock>` commands (esp_console) |
| `main/ui/` | LVGL screens: `ui_watchlist.c` (the two pages and their wheels), `ui_detail.c`, `ui_settings.c` (tabs; add new settings topics there), plus colours, fonts and number formatting in `ui_theme.c` |
| `main/fonts/` | Montserrat with accented letters (Latin-1) and LVGL icons. Regenerate with `tools/gen_fonts.sh` |

**How the endless wheel works** (`ui_watchlist.c`):
- The two pages sit side by side in an LVGL tileview. Each page's list is a normal LVGL scrolling container, one row per stock.
- When a row scrolls out at one end, it is moved to the other end, and the scroll position shifts back by one row height. Nothing visibly jumps.
- The glide runs in `on_refresh_start()`, which LVGL calls at the start of every frame. It scrolls by the distance covered since the previous frame at the slider's speed, keeping the sub-pixel remainder, so motion stays even when frame times vary. The row shifts happen underneath without disturbing it.
- Rows pass their touch events up to the list (`LV_OBJ_FLAG_EVENT_BUBBLE`). That is how a touch anywhere on a list pauses its glide.
- Only the page on screen glides. Sideways drags go to the tileview, because the lists only scroll vertically.
- Timing and sizes are `#define`s at the top of the file (`TOUCH_RESUME_MS`, `ROW_H`).

**Threading rule:**
- Only the LVGL task touches widgets. A 250 ms LVGL timer pulls changes from the model, compared by version counter.
- The quotes task and the console only call model functions, never LVGL. So neither needs the display lock.

## Troubleshooting

- **`ninja failed with exit code -11` (a segfault) after a crash:** the build's `.ninja_deps` file is corrupted. Delete `build/.ninja_deps` and `build/.ninja_log`, or run Full Clean.
- **Status shows "Connecting" forever:** run `wifi` to see the last disconnect reason.
  - 201: the network wasn't found. The ESP32 only sees 2.4 GHz networks.
  - 15 or 202: usually a wrong password.
- **"Yahoo rate limit":** Yahoo returned HTTP 429. The app waits 60 s. Raise the interval if it keeps happening.

## Credits

Emoji images in `main/images/` (chart up, chart down, money bag, flying money, top hat) are from
[Noto Emoji](https://github.com/googlefonts/noto-emoji) (`2D/png/32`), Apache License 2.0. They were
converted with LVGL's `scripts/LVGLImage.py --ofmt C --cf RGB565A8`.
