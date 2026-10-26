# Stock watchlist UI spec (480x480)

The visual spec of the firmware's UI (`main/ui/`, hand-written LVGL C). The LVGL Pro design
project (`lvgl_pro_ui/`) was made from it too. Coordinates are in pixels on the 480x480 panel.

**Revision 4 (2026-10-26):** the spec describes the firmware as built.
- The header has the mood title with an emoji icon, the gear button (Settings), a Wi-Fi icon, the
  time of the last refresh, and a rolling quote banner under the title. The speed slider moved to
  Settings > Scroll Options.
- The title can read "CIRCUIT BREAKER!".
- Two pages, Stocks and Market, swiped sideways. Market tickers are purple. The page name sits in
  the middle of the captions.
- Rows are 92 px high with bigger text, a 112x40 pill with black text, and emoji badges for the
  day's best and worst stock.
- New: the Settings screen.

The LVGL Pro design project still follows revision 3 (see "Known differences" in
[lvgl-pro-guide.md](lvgl-pro-guide.md)).

**Revision 3 (2026-10-23):**
- The header's scroll-mode button is replaced by a speed slider (stopped .. 1.0x, default 0.3x) with a readout.
- The list glides continuously at the chosen speed.
- Status texts are shorter, and the status label sits at the right edge.

**Revision 2 (2026-10-23):**
- The footer bar is gone.
- Rows are taller, with smaller text and names on up to two lines.
- The list rolls like a wheel, endlessly. A header button switches between auto-roll and manual scrolling.
- The title is "Stonks!" or "Not stonks...", coloured by the average change.
- Tickers are gold.
- Adding and removing stocks (the + button, the add overlay and the Remove button) is gone.

## Theme

| Token | Value | Use |
| --- | --- | --- |
| `bg_screen` | `0x0D1117` | Screen and list background, edge fades |
| `bg_header` | `0x161B22` | Header bars, chart background, Settings tab bar, Wi-Fi profile rows, edit card |
| `bg_pressed` | `0x1C2330` | Row or profile row while pressed |
| `divider` | `0x21262D` | 1 px lines between rows and under the captions, chart division lines |
| `track` | `0x30363D` | Unfilled part of the Settings sliders (opaque) |
| `text_primary` | `0xE6EDF3` | Prices, values, the page name |
| `text_secondary` | `0x8B949E` | Company names, captions, the update time, the gear |
| `gold` | `0xF2C94C` | Stock tickers (VAMO3, VALE3, ...), banner text, Wi-Fi icon without internet |
| `purple` | `0xA371F7` | Market tickers (IBOV, USD/BRL, ...) |
| `accent` | `0x2F81F7` | Slider fill, chart with no quote, the active Wi-Fi profile, networks a profile uses |
| `up` | `0x2EA043` | Positive change, "Stonks!", Wi-Fi icon online, "Connected to" |
| `down` | `0xF85149` | Negative change, "Not stonks...", "CIRCUIT BREAKER!" and its banner, Wi-Fi icon offline and its slash, edit errors |
| `flat` | `0x6E7681` | Zero change, no data, "Stonks?" |

LVGL's default theme runs in dark mode, with `accent` as its primary colour, `up` as its
secondary colour and `font_16` as its font (`ui_theme_init()`). Built-in widgets (buttons, tabs,
checkboxes, text areas, the keyboard) take their look from it.

Font: Montserrat Medium, with the Latin-1 range (accents such as "Recebíveis") plus LVGL's icon
symbols (OK, CLOSE, REFRESH, PLUS, LEFT, RIGHT, TRASH, WIFI, WARNING, KEYBOARD, BACKSPACE,
NEW_LINE, UP, DOWN, PLAY, PAUSE, SETTINGS). Sizes and line heights:

| Font | `font_12` | `font_14` | `font_16` | `font_18` | `font_20` | `font_24` | `font_40` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Line height | 16 | 18 | 21 | 23 | 25 | 30 | 49 |

Images: 32x32 Noto Emoji in RGB565A8: chart up, chart down, money bag, flying money, top hat,
lightning.

Number formats:
- Each row has a format that sets how its price and change print:

  | Format | Used for | Price | Change |
  | --- | --- | --- | --- |
  | price | Stocks, EWZ | `61.49` (2 decimals) | `+0.49` |
  | index | IBOV | `205,083` (no decimals, "," between thousands) | `-1,234` |
  | FX | USD/BRL | `4.9652` (4 decimals) | `+0.0123` |
  | rate | DI futures | `13.47%` (2 decimals, % a year) | `+0.05` |

- Change has a sign; zero has none (`0.00`).
- Change % has a sign and 2 decimals: `+0.57%`, `-3.97%`, `0.00%`.
- The trend colour comes from the change %: `up` or `down`, and `flat` when it is less than
  0.005 % from zero or there is no quote.
- Volume is compacted like Investing.com: `224.07K`, `39.59M`, `1.20B`; below 1000 a whole number.
- Times are `16:55`, local time (Brasília, UTC-3), and `--` until the clock is set.
- Before data arrives, values show `--`.
- When the source has no data for a symbol, its name reads "No data (delisted?)" and its pill `n/a`.

## Screen 1: main (watchlist and market)

```
y=0    ┌──────────────────────────────────────────────┐
       │ Stonks! [chart up]      [gear] [wifi] 16:55  │  header h=56, bg_header
y=33   │ The stock market has predicted nine of the   │  banner, scrolls right to left
y=56   ├──────────────────────────────────────────────┤
       │ Symbol         Stocks 1/2       Last   Chg % │  captions h=28
y=84   ├──────────────────────────────────────────────┤  top fade (12 px)
       │ VAMO3 [badge]               6.28 ┌─────────┐ │  row h=92
       │ Vamos Locacao              -0.12 │ -1.88%  │ │
       │                                  └─────────┘ │
       ├──────────────────────────────────────────────┤  1 px divider
       │ WIZC3                       6.97             │
       │ Wiz Co Participacoes e Corretagem            │  name wraps to a 2nd line
       │ de Seguros                 -0.01             │
       │ ...  (about 4.3 rows visible, endless loop)  │  bottom fade (40 px)
y=480  └──────────────────────────────────────────────┘
```

Swiping left shows the Market page: the same layout, with purple tickers.

- **Header (h=56, `bg_header`).**
  - **Title** in `font_24` at (16, 2). It shows the mood of the Stocks page, from the average
    change % of its stocks that have a quote (the Market page doesn't count). The circuit
    breaker comes first:

    | Condition | Text | Colour | Icon |
    | --- | --- | --- | --- |
    | IBOV at -10 % or lower, or `cbtest on` | "CIRCUIT BREAKER!" | `down` | lightning |
    | Average +0.005 % or more | "Stonks!" | `up` | chart up |
    | Average -0.005 % or less | "Not stonks..." | `down` | chart down |
    | Otherwise, or no data yet | "Stonks?" | `flat` | none |

    The 32x32 icon sits 8 px right of the title text, at y=1.
  - **Gear button** 32x32 at (352, 1), transparent, no shadow, touch area extended by 8 px.
    `LV_SYMBOL_SETTINGS` in `font_24` `text_secondary`. Opens Settings.
  - **Wi-Fi icon** `LV_SYMBOL_WIFI` in `font_14` at (402, 8):

    | State | Colour |
    | --- | --- |
    | Online, and the last refresh worked | `up` |
    | Online, but no refresh has worked yet or the last one failed | `gold` |
    | Connecting, or no Wi-Fi configured | `down`, with a 2 px `down` slash from (401, 25) to (421, 8) |

  - **Update time** in `font_14` `text_secondary`, right edge at x=464, y=8: the time of the
    last successful refresh, `--` before the first one.
  - **Banner:** a strip at (16, 33), 448 wide and one `font_14` line high (18 px), that clips
    its text.
    - One line of `font_14` text in `gold` enters at the strip's right edge and moves left at
      40 px/s until it has left at the left edge. Then the next quote starts.
    - Quotes are picked at random from `money_quotes.txt`, never the same one twice in a row.
    - During a circuit breaker they come from `circuit_breaker_quotes.txt`, in `down`. The
      switch happens at once, in both directions.
- **Captions (h=28)** have a 1 px `divider` bottom border. All `font_14`.
  - "Symbol" at x=16, "Last" right-aligned at x=344, "Chg %" right-aligned at x=464, in
    `text_secondary`.
  - The page name, centred, in `text_primary`: "Stocks 1/2" or "Market 2/2".
- **Pages:** an `lv_tileview` at (0, 84), 480x396, with two tiles side by side: Stocks, then
  Market. Swipe left for Market, right to come back. The lists only scroll vertically, so
  sideways drags reach the tileview.
- **List (one per page, 480x396, down to the bottom edge).**
  - It is a wheel: there is no scroll bar, and the rows loop endlessly in both directions.
    A page loops when its rows more than fill it with a spare row at each end
    (rows x 92 >= 396 + 2 x 92); both do today (13 and 7 rows). Otherwise it is a plain list
    with 40 px of bottom padding, so the last row can scroll clear of the bottom fade.
  - Manual scrolling snaps each row to the top edge.
  - **Auto-roll:** the list glides continuously at the speed set in Settings > Scroll Options,
    in tenths: 0 = stopped .. 10 = 1.0x = one row (92 px) per second, default 0.3x. Only the
    page on screen glides, and only when its "Auto-scroll on these pages" box is ticked.
  - Touching the list pauses the glide. It resumes 5 s after the last touch.
  - **Edge fades** suggest a drum. They are non-clickable overlays:
    - top: 12 px at y=84, from `bg_screen` (opaque) to transparent (taller would wash out the top ticker);
    - bottom: 40 px at y=440, from transparent to `bg_screen`.
- **Row (480x92).**
  - The row is `bg_screen`, has a 1 px `divider` bottom border, turns `bg_pressed` while
    pressed, and opens the detail screen when tapped.
  - **Left column:**
    - the ticker in `font_24`, `gold` on the Stocks page and `purple` on the Market page, top at y=10;
    - the badge (Stocks page only), a 32x32 image 8 px right of the ticker text, top at y=9:

      | Badge | Who |
      | --- | --- |
      | Top hat | The day's best stock, above +10 % |
      | Money bag | The day's best stock, above +2 % |
      | Flying money | The day's worst stock, below -2 % |

    - the company name in `font_14` `text_secondary`, top at y=42, width 250. It takes up to 2
      lines (height = 2 line heights) and only then ends in "…".
  - **Middle column (right-aligned, right edge at x=344):**
    - the last price in `font_24` `text_primary`, top at y=10;
    - the absolute change in `font_16`, coloured `up`/`down`/`flat`, top at y=46.
  - **Change % pill:** 112x40, right edge at x=464, vertically centred, radius 8. Its
    background is the trend colour, with black `font_20` text centred (better contrast than
    white on the green and red fills).

## Screen 2: detail (tap a row)

```
y=0    ┌──────────────────────────────────────────────┐
       │ (<)  VALE3   Vale SA                         │  header h=56
y=64   │ 61.49                                        │  font_40
y=112  │ -2.54  (-3.97%)                              │  font_20, trend colour
y=148  │ ┌──────────────────────────────────────────┐ │
       │ │   intraday line chart (15 min closes)    │ │  448x200
       │ └──────────────────────────────────────────┘ │
y=360  │ Open        High        Low                  │  font_14 captions
       │ 63.52       63.52       61.40                │  font_16 values
       │ Prev close  Volume      Time                 │
       │ 64.03       39.59M      16:54                │
y=456  └──────────────────────────────────────────────┘
```

- **Header (h=56, `bg_header`).**
  - The back button is 44x44 at x=8, transparent, no shadow, with `LV_SYMBOL_LEFT` in `font_20` `text_primary`.
  - The ticker is `font_24` at x=60, vertically centred, `gold` or `purple` as in its row.
  - The name is `font_14` `text_secondary`, 260 wide, 12 px right of the ticker and 2 px
    lower, one line ending in "…".
- **Price.** `font_40` `text_primary` at (16, 64). The change line is `font_20` in the trend
  colour at (16, 112): the change and the change % in brackets, or `--` without a quote.
- **Chart.** `lv_chart` line chart at (16, 148), 448x200.
  - The background is `bg_header` with radius 8, 12 px padding, no border, and 4 horizontal
    division lines in `divider`, counting the top and bottom edges.
  - There are no point markers, and the line is 3 px wide.
  - The series colour is the trend colour, or `accent` without a quote.
  - The points are the day's 15-minute closes from Yahoo, at most 40 (thinned out evenly when
    there are more). Chart values are integers: cents, or 1/10000 for USD/BRL. The Y range is
    min - pad .. max + pad, with pad = (max - min) / 10 + 1.
  - With fewer than 2 points, a note in `font_14` `text_secondary` is centred in the chart:
    "Loading intraday chart...", "No intraday chart for this one" (the DI futures) or
    "No data on Yahoo Finance".
- **Stats grid.** At (16, 360), 448 wide: 3 columns of 149 px, 2 rows of 48 px. Each cell has a
  caption in `font_14` `text_secondary` and a value in `font_16` `text_primary` 20 px below it.
  - Open, High, Low, Prev close, Volume, and Time (of the last trade).

## Screen 3: Settings (gear button)

```
y=0    ┌──────────────────────────────────────────────┐
       │ (<)  Settings                                │  header h=56
y=56   ├──────────────────────────────────────────────┤
       │ [wifi] Wi-Fi   Scroll Options   Screen       │  tab bar h=44, font_16
y=100  ├──────────────────────────────────────────────┤
       │ Status      Connected to MyNetwork           │  Wi-Fi tab (scrolls)
       │ IP address  192.168.1.20                     │
       │ Signal      -61 dBm                          │
       │ Profiles: tap one to connect                 │
       │ ┌──────────────────────────────────────────┐ │
       │ │ Work [ok]                       [ Edit ] │ │  profile row h=64
       │ │ MyNetwork                                │ │
       │ └──────────────────────────────────────────┘ │
       │ ...  Home2, Home3, Hotspot - Test            │
       │ [ Scan networks ]                            │
y=480  └──────────────────────────────────────────────┘
```

- **Header (h=56, `bg_header`).** The back button as on the detail screen; "Settings" in
  `font_24` `text_primary` at x=60.
- **Tabs:** an `lv_tabview` at (0, 56), 480x424, background `bg_screen`. The tab bar is at the
  top, 44 px high, `bg_header`, `font_16`: `LV_SYMBOL_WIFI " Wi-Fi"`, "Scroll Options", "Screen".
  - Each tab is a flex column with 16 px padding and 6 px between items, and scrolls when its
    content is taller than the tab.
  - Section captions are `font_14` `text_secondary`. A caption that starts a new section has
    16 px of space above it.
  - While Settings is on screen (and the edit overlay is closed), the tabs refresh every 500 ms.
- **Wi-Fi tab.**
  - **Status block:** three lines, 28 px each: a caption in `font_16` `text_secondary` and its
    value in `font_16` `text_primary` at x=120.
    - Status: "Not configured", "Connecting to <SSID>", or "Connected to <SSID>" in `up`.
    - IP address, and Signal (`-61 dBm`), or `--`.
  - **Profiles:** the caption "Profiles: tap one to connect" (8 px of space above it), then
    four rows, full width, 64 px high, radius 8, `bg_header`, `bg_pressed` while pressed.
    - The profile name in `font_20` at (14, 8), and its SSID in `font_14` `text_secondary` at
      (14, 36), one line, 280 wide, or "(no network set)".
    - The active profile: name in `accent` followed by `LV_SYMBOL_OK`, and a 2 px `accent` border.
    - An "Edit" button, 84x40, 12 px from the right edge, `font_16` white text.
    - Tapping a row connects with that profile and makes it the active one.
  - **Scan:** a 200x40 button, `LV_SYMBOL_REFRESH "  Scan networks"` in `font_16` white, then a
    status line in `font_14` `text_secondary`:
    "Lists the networks the board can see, with signal strength", "Scanning... (a few seconds)",
    or "N networks found (2.4 GHz only), strongest first".
    - Then one 26 px line per network: the SSID in `font_14` (one line, 300 wide; "(hidden)"
      without one), and at the right edge `-61 dBm  ch 6` in `font_14` `text_secondary`.
    - A network a profile uses is in `accent`, with the profile name in brackets.
- **Edit overlay** (Edit button), on the top layer:
  - A full-screen black backdrop at 60 % opacity that blocks taps on the screen behind.
  - A card at (16, 12), 448x216, `bg_header`, radius 12, 16 px padding:
    - the title `Edit profile "Work"` in `font_20` `text_primary`;
    - two one-line text areas, 416x44, `font_16`, at y=36 ("Network name (SSID)", up to 32
      characters) and y=88 ("Password (empty = keep current)", up to 64, shown as dots);
    - a hint in `font_14` `text_secondary` at y=144: "Tap a field to type. OK saves, keyboard
      key cancels." It turns into "The network name can't be empty", or "Could not save:"
      with the error name, in `down`.
  - The keyboard, 480x240 at the bottom edge, `font_20`. OK saves (an empty password keeps
    the saved one) and reconnects if the profile is the active one. The keyboard key closes
    the overlay.
- **Scroll Options tab.**
  - "Auto-scroll speed", then a slider line:
    - The slider is 340x6, 8 px from the left, range 0..10 in tenths, touch area extended by 18 px.
      Track `track` at full opacity (the theme's default is 20 %, invisible here), indicator
      `accent`, knob white with 6 px padding (18 px knob), no shadow.
    - Its value at the right edge in `font_16` `text_primary`: `0.3x`, or `Stop` at 0.
    - The speed applies while dragging and is saved when the finger lifts. It follows the
      `speed` console command.
  - The hint "Stop to 1.0x (one row per second), for both pages".
  - "Auto-scroll on these pages", then two full-width checkboxes in `font_16` with 8 px
    vertical padding: "Stocks" and "Market". Saved when toggled.
- **Screen tab.**
  - "Brightness", then a slider line like the speed one: range 10..100, value `100%`. The
    backlight follows while dragging; the value is saved when the finger lifts.
  - "Orientation", then the checkbox "Upside down (180°)". Unticked, the picture is the
    panel's own orientation turned 180°; ticked, it is the panel's own one. Saved when toggled.
