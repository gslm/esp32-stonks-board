# Stock watchlist UI spec (480x480)

One visual spec shared by the firmware (`02_stock_watchlist`, hand-written LVGL C) and the LVGL Pro
design project (`lvgl_pro_ui/`), so both look the same. Coordinates are in pixels on the 480x480 panel.

**Revision 3 (2026-10-23):**
- The header's scroll-mode button is replaced by a **speed slider** (stopped .. 1.0x, default 0.3x) with a readout.
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
| `bg_screen` | `0x0D1117` | Screen and list background |
| `bg_header` | `0x161B22` | Header bar, chart background |
| `bg_pressed` | `0x1C2330` | Row while pressed |
| `divider` | `0x21262D` | 1 px lines between rows and under the captions |
| `track` | `0x30363D` | Unfilled part of the speed slider (opaque) |
| `text_primary` | `0xE6EDF3` | Prices, values |
| `text_secondary` | `0x8B949E` | Company names, captions, status |
| `gold` | `0xF2C94C` | Tickers (VAMO3, VALE3, ...) |
| `accent` | `0x2F81F7` | Speed slider fill, chart with no trend |
| `up` | `0x2EA043` | Positive change, "Stonks!" |
| `down` | `0xF85149` | Negative change, "Not stonks..." |
| `flat` | `0x6E7681` | Zero change, no data, "Stonks?" |

Font: Montserrat Medium, with the Latin-1 range (accents such as "Recebíveis") plus LVGL's icon
symbols. Sizes: `font_12`, `font_14`, `font_16`, `font_18`, `font_20`,
`font_24`, `font_40`.

Number formats:
- Prices use 2 decimals: `6.28`, `436.55`.
- Change has a sign: `+0.49`, `-2.54`.
- Change % has a sign and 2 decimals: `+0.57%`, `-3.97%`.
- Volume is compacted like Investing.com: `224.07K`, `39.59M`.
- Before data arrives, values show `--`.

## Screen 1: watchlist (main)

```
y=0    ┌──────────────────────────────────────────────┐
       │ Stonks!       ━━━━━●━━━  1.5x     ⌔ 16:55   │  header, h=56, bg_header
y=56   ├──────────────────────────────────────────────┤
       │ Symbol                     Last        Chg %  │  captions, h=28, font_14 text_secondary
y=84   ├──────────────────────────────────────────────┤  ░ top fade (12 px)
       │ VAMO3                      6.28   ┌───────┐  │  row, h=76
       │ Vamos Locacao             -0.12   │-1.88% │  │
       │                                   └───────┘  │
       ├──────────────────────────────────────────────┤  1 px divider
       │ WIZC3                      6.97               │
       │ Wiz Co Participacoes e Corretagem            │  name wraps to a 2nd line
       │ de Seguros                -0.01               │
       │ ...  (about 5.2 rows visible, endless loop)   │  ░ bottom fade (40 px)
y=480  └──────────────────────────────────────────────┘
```

- **Header (h=56).**
  - **Title** in `font_24`, at x=16, vertically centred. It shows the mood of the whole list, from the average change % of every stock that has a quote:

    | Average change | Text | Colour |
    | --- | --- | --- |
    | Above 0 | "Stonks!" | `up` |
    | Below 0 | "Not stonks..." | `down` |
    | Exactly 0, or no data yet | "Stonks?" | `flat` |

  - **Speed slider** `lv_slider` at x=196, 136x6, vertically centred. Range 0..10 in tenths: 0 = stopped, 10 = 1.0x = one row per second, default 3 = 0.3x. Track `track` at full opacity (the theme's default is 20%, invisible here), indicator `accent`, knob white with 6 px padding (18 px knob). Extended click area 18 px.
  - **Speed readout** in `font_14` `text_secondary`, 14 px right of the slider: `1.5x`, or `Stop` at 0.
  - **Status label** in `font_14` `text_secondary`, right-aligned with its right edge at x=464:
    - `LV_SYMBOL_WIFI " 16:55"`: online, showing the time of the last refresh;
    - `LV_SYMBOL_WIFI " ..."`: connecting;
    - `LV_SYMBOL_WARNING " Wi-Fi"` in `down`: Wi-Fi not configured;
    - `LV_SYMBOL_WARNING " Error"` in `down`.
- **Captions (h=28)** have a 1 px `divider` bottom border. All `font_14` `text_secondary`. "Symbol" sits at x=16. "Last" is right-aligned at x=344. "Chg %" is right-aligned at x=464.
- **List (y=84, h=396, down to the bottom edge).**
  - It is a wheel: there is no scroll bar, and the rows loop endlessly in both directions.
  - Manual scrolling snaps each row to the top edge.
  - **Auto-roll:** the list glides continuously at the slider's speed.
  - Touching the list pauses the glide. It resumes 5 s after the last touch. The slider doesn't pause it.
  - **Edge fades** suggest a drum. They are non-clickable overlays:
    - top: 12 px, from `bg_screen` (opaque) to transparent (taller would wash out the top ticker);
    - bottom: 40 px, from transparent to `bg_screen`.
- **Row (480x76).**
  - The row has a 1 px `divider` bottom border, turns `bg_pressed` while pressed, and opens the detail screen when tapped.
  - **Left column:**
    - the ticker in `font_18` `gold`, top at y=10;
    - the company name in `font_12` `text_secondary`, top at y=36, width 250. It takes up to 2 lines (height = 2 line heights) and only then ends in "…".
  - **Middle column (right-aligned, right edge at x=344):**
    - the last price in `font_18` `text_primary`, top at y=10;
    - the absolute change in `font_12`, coloured `up`/`down`/`flat`, top at y=38.
  - **Change % pill:** 96x30, right edge at x=464, vertically centred, radius 8. Its background is the trend colour, with white `font_14` text centred.

## Screen 2: detail (tap a row)

```
y=0    ┌──────────────────────────────────────────────┐
       │ (<)  VALE3   Vale SA                          │  header h=56; ticker in gold
y=64   │ 61.49                                         │  font_40
y=112  │ -2.54  (-3.97%)                               │  font_20, trend colour
y=148  │ ┌──────────────────────────────────────────┐ │
       │ │   intraday line chart (15 min closes)    │ │  448x200
       │ └──────────────────────────────────────────┘ │
y=360  │ Open      High      Low                       │  font_14 captions
       │ 63.52     63.52     61.40                     │  font_16 values
       │ Prev      Volume    Time                      │
       │ 64.03     39.59M    16:54                     │
y=456  └──────────────────────────────────────────────┘
```

- **Header (h=56, `bg_header`).**
  - The back button is 44x44 at x=8, transparent, with `LV_SYMBOL_LEFT` in `font_20` `text_primary`.
  - The ticker is `font_24` `gold` at x=60.
  - The name is `font_14` `text_secondary`, placed after the ticker, one line ending in "…".
- **Price.** `font_40` `text_primary` at (16, 64). The change line is `font_20` in the trend colour at (16, 112).
- **Chart.** `lv_chart` line chart at (16, 148), 448x200.
  - The background is `bg_header` with radius 8 and 4 horizontal division lines in `divider`, counting the top and bottom edges.
  - There are no point markers, and the line is 3 px wide.
  - The series colour is the trend colour.
- **Stats grid.** At (16, 360), 448 wide: 3 columns of 149 px, 2 rows of 48 px. Each cell has a caption in `font_14` `text_secondary` and a value in `font_16` `text_primary` below it.
