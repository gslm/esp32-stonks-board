# Session state

Living checkpoint of in-flight work (see DIRECTIVES.md, rule 6). Keep it short and current.
Last updated: 2026-10-23, end of day.

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

## Local only (this workstation)

- The LVGL perf monitor log (`LV_USE_SYSMON`, `LV_USE_PERF_MONITOR`, `..._LOG_MODE`,
  `LV_USE_LOG`) is on in the untracked `sdkconfig`, and the board is flashed with it. To turn it
  off: delete `sdkconfig` and rebuild (defaults have it off), then flash.

## Next (operator to decide)

- JTAG debugging over the USB-OTG port.
- Moving the firmware UI to LVGL Pro exported code (needs `CONFIG_LV_USE_OBJ_NAME=y`, and
  removing `main/fonts/`; see docs/lvgl-pro-guide.md). The design project is behind the
  firmware: it lacks the banner, the bigger rows and the emoji badges.
