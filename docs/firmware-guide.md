# Firmware guide

How the stock board firmware works, why it is built the way it is, and how to use it to learn
ESP-IDF and LVGL. Read it next to the code: every section names the files and functions to open.

Versions this guide describes: ESP-IDF v6.0.1, LVGL 9.5.0, esp_lvgl_port 2.9.0, Waveshare BSP
`waveshare/esp32_s3_touch_lcd_4b` 2.0.0 (all pinned in `dependencies.lock`).

## Contents

1. [Architecture decisions](#1-architecture-decisions)
2. [Hardware platform](#2-hardware-platform) (to be written)
3. [Components](#3-components) (to be written)
4. [Files and functions](#4-files-and-functions) (to be written)
5. [Other things worth knowing](#5-other-things-worth-knowing) (to be written)

---

## 1. Architecture decisions

Each decision below says what was chosen, why, what else was considered, and where to find it
in the code. They are grouped from the bottom of the stack (framework, tasks, memory) to the top
(screens and features).

### 1.1 Framework: plain ESP-IDF with the component manager

- **Chosen:** ESP-IDF v6.0.1, the vendor SDK, built with `idf.py` (CMake + Ninja). No Arduino
  layer.
- **Why:** full access to FreeRTOS, the LCD and Wi-Fi drivers, NVS and the console, and the same
  APIs Espressif's own examples and the BSP use. The VSCode ESP-IDF extension and clangd give
  Ctrl+click navigation into the IDF sources.
- **Libraries come from the IDF Component Manager.** `main/idf_component.yml` lists the direct
  dependencies; the first build downloads them into `managed_components/` (not tracked) and
  records the exact versions in `dependencies.lock` (tracked). The direct ones:
  - `waveshare/esp32_s3_touch_lcd_4b` 2.0.0: the board support package (BSP).
  - `lvgl/lvgl` 9.5.0, pinned exactly. The BSP accepts any LVGL from 8 up to (not including) 10,
    so a loose pin would pull 9.6, which moves headers and changes APIs.
  - `espressif/cjson`: ESP-IDF 6 no longer ships cJSON as a built-in component.
  - Everything else (`esp_lvgl_port`, the ST7701 and GT911 drivers, the IO expander driver) comes
    in as dependencies of the BSP.
- **Never edit `managed_components/`.** The component manager overwrites it. Where the BSP does
  something we don't want, the firmware works around it from our own code (see 1.12).
- **Where:** `main/idf_component.yml`, `dependencies.lock`, `CMakeLists.txt` (project),
  `main/CMakeLists.txt` (the `main` component: its sources, include folders and embedded files).

### 1.2 Board support: the Waveshare BSP plus esp_lvgl_port

- **Chosen:** let the BSP bring up the display, touch, backlight and LVGL with one call,
  `bsp_display_start()` in `app_main()`.
- **What that one call does:** initialises the I2C bus and the TCA9554 IO expander, resets the
  panel, sends the ST7701 init commands over 3-wire SPI, creates the RGB panel with its frame
  buffers, starts esp_lvgl_port (the LVGL task and tick), registers the display with LVGL, and
  creates the GT911 touch input device.
- **Why:** the board needs a long, board-specific init sequence (pins, panel timings, vendor init
  commands, IO expander resets). The BSP is Waveshare's tested version of it.
- **Cost:** some BSP choices are fixed at compile time through Kconfig (`CONFIG_BSP_*`), and a few
  are awkward (it deletes the panel's command channel after init, see 1.12; its brightness getter
  returns the inverted value).
- **Where:** `main/main.c` (`app_main`), `sdkconfig.defaults` (the `CONFIG_BSP_*` lines).

### 1.3 Tasks and the threading rule

The firmware runs a handful of FreeRTOS tasks. The ones that matter:

| Task | Created by | Priority | Stack | Job |
| --- | --- | --- | --- | --- |
| LVGL (`taskLVGL`) | esp_lvgl_port | 4 | 7 KB, internal RAM | Runs LVGL timers, input and drawing; our UI code runs here |
| `swdraw` x2 | LVGL (`LV_OS_FREERTOS`) | 3 | 8 KB each | Software render threads, one per draw unit (both cores) |
| `quotes` | `quotes_start()` | 3 | 12 KB, internal RAM | All HTTPS requests and JSON parsing |
| console REPL | `esp_console` | 2 | 4 KB | The `stock>` command line on UART0 |
| `wifi_scan` | `wifi_scan_start()` | 4 | 3 KB | Short-lived: runs one scan, then deletes itself |
| `sys_evt`, `wifi`, `esp_timer`, ... | ESP-IDF | various | - | Event loop (our Wi-Fi handlers run here), Wi-Fi driver, timers |

`app_main()` itself only starts things and returns; its task then ends.

**The threading rule: only the LVGL task touches LVGL objects.**

- LVGL is not thread-safe. The usual alternative is to take the display lock
  (`bsp_display_lock()`) from any task that changes a widget. That spreads locking all over the
  code, and a task that holds the lock while it waits on the network freezes the screen.
- Instead, other tasks never call LVGL. They write into the **model** (`stock_model.c`), which has
  its own mutex. The UI **pulls**: a 250 ms LVGL timer (`ui_tick()` in `ui/ui.c`) asks the model
  for its version counter and only redraws when it changed.
- The model keeps two counters: `model_list_version()` changes when the set of rows changes (only
  at start-up today), `model_data_version()` on any change (prices, status). Comparing a counter
  is cheap, so polling every 250 ms costs almost nothing when nothing changed.
- Readers take **copies** (`model_snapshot()`, `model_get()`), so the mutex is held only for a
  `memcpy`, never while drawing or doing network I/O.
- The other direction (UI -> quotes task) is also a non-blocking request: `quotes_request_detail()`
  stores the symbol under a spinlock and notifies the task (`xTaskNotify`).
- The one exception: `app_main()` builds the UI under `bsp_display_lock()`, because at that
  moment the LVGL task already exists.

### 1.4 Data model: fixed lists, shared behind one mutex

- **Chosen:** two fixed tables compiled into the firmware: `DEFAULT_STOCKS` (the 13 B3 stocks of
  the Investing.com "Ativos" watchlist) and `MARKET` (Ibovespa, dollar, EWZ, four DI futures).
  `model_init()` copies them into one array of `stock_t`, watchlist first, then market.
- **Why fixed:** editing a watchlist on a 480x480 touch screen is clumsy, and the operator
  manages the real list on Investing.com anyway. An add/remove screen existed early on and was
  removed for simplicity. To change the list: edit the table, rebuild.
- **One record type for both pages.** `stock_t` carries a `group` (Stocks or Market), a display
  `format` (price, index, FX, rate), and a data `source` (Yahoo or TradingView). The UI, the
  fetcher and the console all work on the same array and branch on those fields.
- **Order is part of the contract:** watchlist rows come first, then market rows. The main screen
  splits the snapshot at the first market row.
- **Where:** `main/stock_model.h`, `main/stock_model.c`.

### 1.5 Data sources: Yahoo Finance and TradingView, no API keys

- **The goal was the Investing.com watchlist, but Investing.com can't be used directly.** It has
  no public API, its terms forbid automated extraction, and automated requests get HTTP 403.
  So the list is copied by hand into `DEFAULT_STOCKS`, and prices come from elsewhere.
- **Chosen for stocks, index, dollar and EWZ:** Yahoo Finance's public chart endpoints, which need
  no account or key.
  - `/v8/finance/spark`: last price and previous close for up to 20 symbols in one request. Used
    every refresh (default 30 s).
  - `/v8/finance/chart/<symbol>?range=1d&interval=15m`: day range, volume, name, and the intraday
    series for the detail chart. One symbol per request, so it runs round-robin (one per refresh)
    and on demand when a detail screen opens.
- **Chosen for the DI rate futures:** Yahoo doesn't carry them. TradingView's public scanner
  endpoint (`scanner.tradingview.com/futures/scan`, a JSON POST) returns price, change, open,
  high, low, volume and update time for all four contracts in one request.
- **Delay:** both sources are about 15 minutes behind for B3. Free real-time B3 data doesn't exist
  (Investing.com's free pages are delayed the same way).
- **Caveat:** neither Yahoo's nor TradingView's terms cover automated access. Fine for a personal
  demo; a product would need a licensed API. `docs/research-notes.md` lists the options looked at
  (brapi.dev, Finnhub, EODHD, Twelve Data).
- **Where:** `main/quotes.c`, `main/quotes.h`.

### 1.6 Networking: one task, one HTTP client, PSRAM for the bulky parts

- **One task does all network I/O** (`quotes_task()`), so no other task ever blocks on the
  network, and requests never run in parallel (simpler, and gentler on Yahoo's rate limits).
- **One `esp_http_client`, kept alive.** Re-using the TLS connection saves a TLS handshake (slow,
  and heavy on internal RAM) per request. When the host changes (TradingView -> Yahoo) the client
  closes and reopens by itself. The order inside a refresh is TradingView first, then Yahoo, so
  the Yahoo connection stays open for the detail requests that follow.
- **HTTPS with ESP-IDF's certificate bundle** (`esp_crt_bundle_attach`,
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`): the common root CAs are built in, so no
  certificate files to manage. Yahoo chains to DigiCert Global Root G2; TradingView to Amazon /
  Starfield roots; both are in the bundle.
- **Memory:** the 48 KB response buffer is allocated in PSRAM, and cJSON is told to allocate in
  PSRAM too (`cJSON_InitHooks()` with `psram_malloc()`), because a parse makes hundreds of small
  allocations that would fragment internal RAM. The task stack stays in internal RAM, because
  mbedTLS needs a deep, fast stack.
- **Rate limits:** on HTTP 429 the task waits 60 s (`BACKOFF_S`) before the next attempt.
- **Failure policy:** a failed Yahoo refresh sets the status error (the Wi-Fi icon turns yellow);
  a failed TradingView request is only logged, and the DI rows just keep their last values.
- **Where:** `main/quotes.c` (`http_request()`, `refresh_cycle()`, `quotes_task()`).

### 1.7 Wi-Fi: station mode, four profiles, own reconnect timer, SNTP

- **Station only**, started from `wifi_start()`. Event handlers (`on_wifi_event()`,
  `on_ip_event()`) run in ESP-IDF's default event loop task.
- **Credentials live in our own NVS namespace**, as four named profiles (Work, Home2, Home3,
  Hotspot - Test), one of them active. The driver is told not to store them
  (`esp_wifi_set_storage(WIFI_STORAGE_RAM)`), so it doesn't write flash on every connect.
- **Default passwords are not in the repo** (it is public). They come from
  `main/wifi_profiles_local.h`, which is git-ignored; a tracked template
  (`wifi_profiles_local.example.h`) is used when it is missing. Edits made on the device are saved
  in NVS and survive reflashing.
- **Reconnect:** after a disconnect, a one-shot `esp_timer` retries in 5 s. A disconnect we caused
  ourselves while switching networks (`WIFI_REASON_ASSOC_LEAVE`) is not retried.
- **Getting an IP address** sets the model to online and asks the quotes task to refresh at once.
- **Clock:** SNTP (`pool.ntp.org`) sets the time; `app_main()` sets the time zone to Brasilia
  (UTC-3, `setenv("TZ", "<-03>3")`). Times before 2024 print as "--", which hides the 1970 clock
  before SNTP has answered.
- **Scan:** a short-lived task starts the scan (pausing the reconnect timer, which would abort it);
  the results arrive as `WIFI_EVENT_SCAN_DONE`, are sorted by signal strength and copied under a
  spinlock for the Settings screen to read.
- **Where:** `main/wifi.c`, `main/settings.c`.

### 1.8 Settings persistence: NVS, cached in RAM

- **Chosen:** ESP-IDF's NVS (a key-value store in the `nvs` flash partition), namespace
  `settings`. Values used every frame (the roll speed, the auto-scroll switches) are cached in
  RAM, so reading them costs nothing.
- **Write rarely.** Flash writes stall the caches, so sliders apply their value while you drag
  but save only when you let go (`LV_EVENT_RELEASED`).
- **Keys today:**

| Key | Type | Meaning | Default |
| --- | --- | --- | --- |
| `wifi_active` | u8 | Active Wi-Fi profile, 0..3 | 0 (Work) |
| `wifi0_ssid` .. `wifi3_ssid`, `wifi0_pass` .. `wifi3_pass` | string | Wi-Fi profiles | from `wifi_profiles_local.h` |
| `refresh_s` | i32 | Refresh interval, 10..3600 s | 30 (`CONFIG_STOCK_REFRESH_INTERVAL_S`) |
| `roll_speed` | u8 | Glide speed in tenths, 0..10 | 3 (0.3x) |
| `autoscroll0`, `autoscroll1` | u8 | Auto-scroll on the Stocks / Market page | 1 (on) |
| `brightness` | u8 | Backlight %, 10..100 | 100 |
| `orientation` | u8 | 1 = "Upside down" ticked (panel's own orientation) | 0 (panel turned 180°) |

- **Changing the meaning of a setting means a new key.** The orientation used to be stored as
  `upside_down` with the opposite meaning; it moved to `orientation` so an old saved value can't
  come back with the wrong meaning. Keys left behind by older versions (`upside_down`, and
  `auto_scroll` from before the repo existed) are simply ignored.
- **Where:** `main/settings.c`, `main/settings.h`.

### 1.9 Memory placement: PSRAM for bulk, internal RAM for speed

The ESP32-S3 has about 512 KB of internal SRAM and, on this module, 8 MB of octal PSRAM.

- **In PSRAM:** the two 480x480 RGB565 frame buffers (450 KB each), the HTTP response buffer,
  cJSON's allocations, and large static arrays marked `EXT_RAM_BSS_ATTR` (the model snapshots in
  `quotes.c`, `cli.c`, `ui_watchlist.c`). That attribute needs
  `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`.
- **In internal RAM:** task stacks (TLS especially), Wi-Fi buffers, and DMA descriptors. Wi-Fi
  and mbedTLS fail in confusing ways when internal RAM runs short, which is why bulky data is
  pushed out.
- **Code runs from PSRAM** (`CONFIG_SPIRAM_XIP_FROM_PSRAM=y`). While flash is being written (NVS),
  the flash cache is disabled; with code in PSRAM the CPU and the RGB panel's DMA keep running,
  so the screen doesn't glitch when a setting is saved.
- **Where:** `sdkconfig.defaults` (the PSRAM block), `EXT_RAM_BSS_ATTR` uses in the sources.

### 1.10 Display pipeline: direct mode, two frame buffers, two render threads

The biggest performance work. The list glide went from about 11 FPS to 19.5 FPS:

- **Direct mode with anti-tearing** (`CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE`,
  `CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR`, `CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2`): LVGL draws straight
  into the hidden one of two full frame buffers, and the RGB driver swaps them at the end of a
  refresh. No copy from a draw buffer into the frame buffer, and no tearing.
  Measured on the way there: 14 FPS with a 100-line draw buffer, 15.6 with a 480-line one.
- **Two software render threads** (`CONFIG_LV_OS_FREERTOS`, `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2`),
  so drawing uses both CPU cores.
- **The glide moves once per frame,** in `on_refresh_start()` (a display `LV_EVENT_REFR_START`
  handler), by the time elapsed since the previous frame, keeping the sub-pixel remainder. Moving
  it from a fixed timer to the frame start made the motion even.
- **Tried and dropped:** a second draw buffer (esp_lvgl_port copies synchronously, so it gains
  nothing), PSRAM at 120 MHz (needs 120 MHz flash too; experimental).
- **Consequence:** software rotation by 90° is off the table (see 1.12), because esp_lvgl_port's
  software rotation doesn't work with direct mode.
- **Where:** `sdkconfig.defaults` (display and LVGL blocks), `ui/ui_watchlist.c`
  (`on_refresh_start()`, `kick_tick()`).

### 1.11 UI structure: three screens, pulled from the model

- **Screens:** the main screen (`ui_watchlist.c`), the detail screen (`ui_detail.c`) and Settings
  (`ui_settings.c`). All three are created once at start-up by `ui_init()` and switched with
  `lv_screen_load()`. Nothing is created or deleted while navigating, which keeps memory steady.
- **Main screen pages:** an `lv_tileview` with two tiles side by side, Stocks and Market. Swiping
  sideways changes tile; the lists inside only scroll vertically, so LVGL passes sideways drags up
  to the tileview by itself.
- **The endless wheel:** each page's list is an ordinary scrolling container. When a row leaves
  the view at one end it is moved to the other end and the scroll position is shifted by one row,
  so nothing visibly jumps and the list never reaches an edge (`wheel_normalize()`). One `page_t`
  struct per page holds the list, its rows and its touch state.
- **Settings:** an `lv_tabview` with one tab per topic (Wi-Fi, Scroll Options, Screen). A new topic
  is a new `create_*_tab()` function plus one `lv_tabview_add_tab()` line.
- **Shared look:** colours, fonts and number formatting live in `ui_theme.c` / `ui_theme.h`, so the
  screens don't repeat hex values or format code. Gold tickers for stocks, purple for market rows.
- **Hand-written, not generated.** The UI was first sketched in LVGL Pro Editor (`lvgl_pro_ui/`),
  but the firmware code is written by hand; the design project now lags behind it.

### 1.12 Orientation and touch: let the panel turn the picture

- **180° is done by the ST7701 panel controller**, which can reverse its column order (SDIR's SS
  bit) and line order (MADCTL's ML bit). Drawing costs nothing extra, and direct mode stays intact.
- **The BSP deletes the panel's command channel after init.** That channel is a 3-wire SPI that runs
  through the IO expander. `send_flip()` in `screen.c` creates it again for the four commands and
  deletes it afterwards.
- **Touch follows** in a small wrapper around esp_lvgl_port's read callback (`touch_read()`): it
  flips x and y while the picture is turned.
- **Touch X correction:** test taps showed the GT911 stretches X towards the side edges (taps in
  the outer ~28 px all read 0 or 479). The wrapper squeezes X into 20..459 (`TOUCH_X_MARGIN`);
  Y is accurate and left alone.
- **90° was dropped:** the panel has no 90° mode, and software rotation breaks direct mode (1.10).
- **The default orientation** is the panel's own one turned 180° (the way the operator holds the
  board). "Upside down" in Settings > Screen gives the panel's own orientation.
- **Where:** `main/screen.c`, `main/screen.h`.

### 1.13 Fonts and images: generated once, committed

- **Fonts:** LVGL's built-in Montserrat has no accented letters, and the stock names are
  Portuguese. `tools/gen_fonts.sh` runs `lv_font_conv` (via `npx`) to build Montserrat Medium with
  Latin-1, a few typographic characters, and the FontAwesome icons LVGL uses (`LV_SYMBOL_*`), at
  12, 14, 16, 18, 20, 24 and 40 px. The generated C files in `main/fonts/` are committed, so
  building needs no Node.js.
- **Images:** the emoji badges and title icons are Noto Emoji PNGs (32x32), converted with LVGL's
  `scripts/LVGLImage.py` to C arrays in RGB565A8 (16-bit colour plus an 8-bit alpha plane).
  They need the compile definition `LV_LVGL_H_INCLUDE_SIMPLE` (set in `main/CMakeLists.txt`).
- **Text files built in:** `money_quotes.txt` and `circuit_breaker_quotes.txt` are embedded with
  `EMBED_TXTFILES`; the linker exposes them as `_binary_<name>_start` symbols. Edit, rebuild.

### 1.14 Console: UART0 only, logs on both ports

- **The board has two USB-C ports:** USB-OTG (the S3's own USB-Serial/JTAG) and USB TO UART
  (a CH343 bridge to UART0).
- **Logs go to both** (`CONFIG_ESP_CONSOLE_UART_DEFAULT` plus
  `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`). ESP-IDF's secondary console is output only, so
  the `stock>` command line (esp_console REPL) only takes input on USB TO UART.
- **Why keep it:** it is a backup for everything the screen does (Wi-Fi credentials, speed,
  interval), plus test hooks like `cbtest`.
- **Where:** `main/cli.c`, `sdkconfig.defaults` (console block).

### 1.15 Circuit breaker: derived from the index

- B3 halts trading when the Ibovespa falls 10 % below the previous close. No free source publishes
  the halt itself, so `model_circuit_breaker()` treats "IBOV at -10 % or lower" as a circuit
  breaker, or the `cbtest on` override. The title and banner react to it like to any other model
  change.
- **Where:** `main/stock_model.c`, `ui/ui_watchlist.c` (`update_title()`), `ui/ui_banner.c`
  (`ui_banner_set_alert()`).

### 1.16 Repository hygiene

- **Public repo, no secrets:** Wi-Fi passwords only in the git-ignored `wifi_profiles_local.h`.
- **No absolute paths in tracked files:** `.vscode/settings.json` is per machine and ignored; the
  tracked `settings.example.json` uses `${userHome}` and `${workspaceFolder}`.
- **Generated and downloaded things stay out:** `build/`, `managed_components/`, `sdkconfig`.
  `sdkconfig.defaults` is the tracked source of the configuration; `sdkconfig` is regenerated from
  it when missing.
- **`SESSION-STATE.md`** records in-flight work and decisions, so they survive between sessions.

---

## 2. Hardware platform

(to be written)

## 3. Components

(to be written)

## 4. Files and functions

(to be written)

## 5. Other things worth knowing

(to be written)
