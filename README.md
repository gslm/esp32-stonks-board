# 02_stock_watchlist

Shows the Investing.com "Ativos" watchlist (13 B3 tickers) on the 480x480 screen, with prices from
Yahoo Finance refreshed every 30 s.

- **The list is an endless wheel** that glides continuously and loops forever.
  - The header slider sets the speed, from stopped to 1.0x (one row per second). Default 0.3x.
  - The setting is saved when you let go. The `speed` command does the same.
  - Drag the list to scroll by hand; it also loops endlessly and snaps to a row.
  - Touching the list pauses the glide, which resumes 5 s later. Touching the slider doesn't pause it.
- **The title shows the mood.** It reads "Stonks!" in green when the average change % of the list is positive, and "Not stonks..." in red when it's negative.
- **The title gets a chart icon:** up for "Stonks!", down for "Not stonks...".
- **Badges after the ticker:** the day's best performer gets a money bag above +2%, or a top hat above +10%. The worst performer gets flying money below -2%. The thresholds are `MONEYBAG_PCT`, `TOPHAT_PCT` and `LOSER_PCT` in `main/ui/ui_watchlist.c`.
- **A gold banner** along the bottom of the top bar scrolls a random quote from `money_quotes.txt` (one per line). A new quote is picked each time one has scrolled off. The file is built into the firmware, so edit it and rebuild.
- **Tap a row** to see the stock's day range, volume and an intraday chart.
- **The Wi-Fi icon** (top right) is green when online, yellow when on Wi-Fi without internet (the last refresh failed), and red with a slash when not connected.
- **The gear button** (top right) opens Settings. Its Wi-Fi tab shows the connection, plus four Wi-Fi profiles (Work, Home2, Home3, Hotspot - Test): tap one to connect, or Edit to change its network and password.

The watchlist is fixed in `main/stock_model.c` (`DEFAULT_STOCKS`); edit it there and rebuild.

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
| `list` | Watchlist with last price, change and change % |
| `wifi` / `wifi "ssid" "pass"` | Show the Wi-Fi status / set new credentials for the active profile |
| `interval 15` | Refresh every 15 s (10..3600) |
| `speed 0.5` | Glide speed, 0 (stopped) to 1.0 (one row per second). The header slider follows it. Saved |
| `refresh` | Fetch now |
| `status` | Network, last update, errors, free memory |

## Data source and limits

- **Source:** Yahoo Finance's public chart endpoints. They need no account or API key.
  - Every refresh, one request gets the last price and previous close for all symbols, about 3 KB.
  - Per-symbol requests, round-robin and when a detail screen opens, get open/high/low/volume and the 15-minute intraday series.
- **Delay:** B3 prices on Yahoo are about **15 minutes delayed**. Investing.com's free pages are delayed the same way (the red clock icon), and free real-time B3 data doesn't exist. Refreshing more often than every 30 s only picks up the delayed feed sooner.
- **Terms:** Yahoo's terms don't cover automated access. Fine for a personal demo; for anything more, switch to a licensed API (see [docs/research-notes.md](docs/research-notes.md)).

## Code map

| File | Role |
| --- | --- |
| `main/main.c` | Startup order: NVS, model, display + UI, Wi-Fi, quote fetcher, console |
| `main/stock_model.c` | The watchlist (`DEFAULT_STOCKS`) and its quotes, shared by all tasks behind one mutex |
| `main/quotes.c` | The only task that talks to the network: HTTPS (`esp_http_client` + certificate bundle) and JSON parsing (cJSON) |
| `main/wifi.c` | Station mode, reconnect every 5 s, SNTP clock (Brasília time) |
| `main/settings.c` | Wi-Fi profiles, refresh interval and glide speed in NVS. Profile defaults come from `main/wifi_profiles_local.h` (git-ignored; template `wifi_profiles_local.example.h`) |
| `main/cli.c` | The `stock>` commands (esp_console) |
| `main/ui/` | LVGL screens: `ui_watchlist.c` (the wheel), `ui_detail.c`, `ui_settings.c` (tabs; add new settings topics there), plus colours, fonts and number formatting in `ui_theme.c` |
| `main/fonts/` | Montserrat with accented letters (Latin-1) and LVGL icons. Regenerate with `tools/gen_fonts.sh` |

**How the endless wheel works** (`ui_watchlist.c`):
- The list is a normal LVGL scrolling container, one row per stock.
- When a row scrolls out at one end, it is moved to the other end, and the scroll position shifts back by one row height. Nothing visibly jumps.
- The glide runs in `on_refresh_start()`, which LVGL calls at the start of every frame. It scrolls by the distance covered since the previous frame at the slider's speed, keeping the sub-pixel remainder, so motion stays even when frame times vary. The row shifts happen underneath without disturbing it.
- Rows pass their touch events up to the list (`LV_OBJ_FLAG_EVENT_BUBBLE`). That is how a touch on the list pauses the glide while a touch on the slider doesn't.
- Timing and sizes are `#define`s at the top of the file (`TOUCH_RESUME_MS`, `ROW_H`, `SLIDER_W`).

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
