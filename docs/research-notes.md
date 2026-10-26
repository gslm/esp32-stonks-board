# Research notes (2026-10-22)

Findings gathered while setting up the first test app (hello counter, since removed). They feed the next steps: the UI design
workflow and the stock-price app.

**Status (2026-10-26).** These notes are a snapshot, kept for their sources and reasoning. Where
the project went since (details in [firmware-guide.md](firmware-guide.md)):
- **UI:** the firmware UI is hand-written LVGL C (`main/ui/`). The design project
  (`lvgl_pro_ui/`) uses LVGL Pro Editor, not SquareLine Studio, and the `lv_subject` binding
  plan below was not used: a 250 ms LVGL timer pulls from the data model instead.
- **Display:** two frame buffers in PSRAM, with LVGL in direct mode (guide section 1.10).
- **Data:** the device polls Yahoo Finance (stocks, Ibovespa, USD/BRL, EWZ) and TradingView's
  public scanner (DI futures) itself, every 30 s by default, with no proxy and no API keys.
- **Open questions** (at the end), as the firmware has them today: B3 stocks plus Brazilian
  market indicators; 15-minute delayed data refreshed every 30 s; prices and daily change
  only, no holdings or P&L.

## Board facts that matter

- **Module:** ESP32-S3-WROOM-1-N16R8, with 16 MB quad flash and 8 MB octal PSRAM. GPIO35-37 are taken by the PSRAM.
- **Display:** 480x480 ST7701, 16-bit RGB565 parallel bus with a 16 MHz pixel clock.
  - The panel's init commands go over 3-wire SPI, bit-banged through a TCA9554 IO expander (I2C 0x20).
  - The frame buffer is in PSRAM, plus a 20-line bounce buffer.
- **Touch:** GT911 at I2C 0x5D (SDA 47, SCL 48). It has no interrupt line to the ESP32, so touch is polled.
- **Backlight:** GPIO4 PWM, inverted: high duty = dim. `bsp_display_brightness_set()` handles the inversion.
  `bsp_display_brightness_get()` returns the flipped value (a BSP bug).
- **Power:** the AXP2101 PMU supplies the 3.3 V rail. Display and touch need no PMU code.
  Audio (ES8311/ES7210) and battery charging do.
- **Buttons:** BOOT (GPIO0) and PWR (AXP2101). There is no RESET button.
- **USB-C ports:** "USB-OTG" is the native USB-Serial/JTAG (flash, console, JTAG). "USB TO UART" is a CH343 bridge to UART0, with auto-reset.
- **IO expander pins:** EXIO3 speaker amp, EXIO4 PWR-key sense, EXIO5/6 GT911 reset/INT, EXIO7 LCD reset.

## Software baseline

- ESP-IDF v6.0.1, BSP `waveshare/esp32_s3_touch_lcd_4b` 2.0.0, LVGL 9.5.0, esp_lvgl_port 2.9.0.
  Waveshare's CI builds its examples on IDF v5.5.5 and v6.0.2.
- **Keep LVGL pinned to exactly `9.5.0`.** The BSP accepts `>=8,<10`, so a loose pin pulls 9.6.
  9.6 moves headers, deprecates the `lv_subject_init_*` API, and doesn't match the UI tools below.
- IDF 6 removed the built-in `json` and `mqtt` components. Add `espressif/cjson` and `espressif/mqtt`
  from the registry when needed.
- The official examples have no Wi-Fi, HTTPS, SNTP or OTA code. Take those from ESP-IDF's own
  examples: `wifi/getting_started/station`, `protocols/esp_http_client`, `protocols/sntp`,
  `system/ota/simple_ota_example`.
- **Keep `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`.** The RGB panel in bounce-buffer mode can't keep running
  while flash writes (NVS, OTA) disable the cache unless code runs from PSRAM.
  Even with it, write NVS rarely: on configuration changes, never on every price update.

## Designing LVGL screens

Recommended: **SquareLine Studio 1.6.2**, already installed in `~/Downloads/SquareLine_Studio_Linux_v1_6_2`.

- It bundles LVGL 9.5.0, the exact firmware version.
- The free Personal licence covers a non-commercial project: 10 screens, 150 widgets.
- **Project settings:** 480x480, colour depth 16 (not "16 sw"), LVGL 9.5, include path `lvgl.h`.
  - Base it on the Makerfabs "E32S3RGB40 MaTouch 4-inch 480x480 ESP-IDF" template, or a Desktop template.
  - The Espressif EV-Board template is fixed at 800x480.
- **Limits:**
  - It generates no data bindings.
  - Its 9.5 widget set has no table, list or scale.
  - It still emits LVGL 8 alias names (`lv_disp_*`); these work on 9.5.
- **Keep the generated code as a pure view.**
  - Export it to `components/ui/generated/`, wrapped by a hand-written `components/ui/CMakeLists.txt`.
  - Put the data in LVGL 9 `lv_subject` observers. Do the binding in one hand-written file (`main/ui_bind.c`).
  - Build dynamic parts (stock rows, charts) in C.

Alternatives:

- **LVGL Pro Editor 2.0.5:** official, free Community tier, supports 9.5. The UI is XML text that can be edited directly, and it has native subject binding.
  Needs a login and `CONFIG_LV_USE_OBJ_NAME=y`. The best upgrade path if SquareLine's limits start to hurt.
- **EEZ Studio 0.29:** open source, supports 9.5. Use its plain LVGL mode, not Flow mode.
- **Avoid:** NXP GUI Guider (LVGL 9.4 only) and PicoPixel (LVGL 8 only).
- **Faster iteration:** the PC simulator `lv_port_pc_vscode`, branch `release/v9.5`, with its `lvgl` submodule checked out at `v9.5.0`.

## Stock data

- **Investing.com can't be linked to the device.**
  - It has no public API, and its Terms §10(c) forbid automated extraction.
  - Automated requests get HTTP 403.
  - investpy and investiny are abandoned.
  - Usable path: keep managing the watchlist there, export it as CSV (Download Portfolio), and fetch prices from a licensed source.
- **No free source checked offers real-time prices for non-US exchanges.**
  - Brazil (B3): Yahoo `.SA` symbols are 15 min delayed. brapi.dev is free at 30 min delay, or paid at 15 or 5 min.
  - Euronext: Yahoo or EODHD REST are 15-20 min delayed. EODHD's $29.99/mo plan adds a European WebSocket.
  - US: Finnhub free gives real-time REST (60 calls/min) and a WebSocket.
  - Twelve Data's free tier is licensed "non-display" only, so it can't drive a display.
- **Recommended architecture:**
  - Phase 1: the device polls one HTTPS endpoint every 30-60 s, using `esp_http_client`, the certificate bundle and `espressif/cjson`.
  - Phase 2: a small proxy on an always-on machine at home polls or streams the providers and pushes compact JSON to the device over MQTT or HTTP. This keeps API keys off the device.
- **TLS gotcha:** finnhub.io and brapi.dev chain to GTS Root R4, cross-signed by GlobalSign R1, which isn't in IDF 6.0.1's bundle.
  Enable `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`. Yahoo chains to DigiCert G2, which is in the bundle.

## Open questions for the stock app

- Which exchanges are the holdings on?
- How fresh must prices be? Tick-by-tick, or a 30-60 s refresh with delayed non-US data?
- Is there an always-on machine for a proxy (Raspberry Pi, NAS, Home Assistant)?
- Prices and daily change only, or holdings value and P&L too?

## Reference repos

- **[waveshareteam/ESP32-S3-Touch-LCD-4B](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-4B):** official.
  - `02_lvgl_demo_v9` is the ESP-IDF base.
  - `01_HelloWorld` is Arduino (Arduino_GFX) only.
  - The schematic is in `hardware/schematics/`.
- **[alaltitov/Waveshare-ESP32-S3-Touch-LCD-4B](https://github.com/alaltitov/Waveshare-ESP32-S3-Touch-LCD-4B):** ESPHome (YAML) Home Assistant panel.
  - Its pins and timings match the BSP.
  - UI patterns worth copying: dark cards, a status row, bottom navigation, dim-on-idle, and boot and OTA overlays.
