# Session state

Living checkpoint of in-flight work (see DIRECTIVES.md, rule 6). Keep it short and current.
Last updated: 2026-10-25.

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

## In progress: Settings screen (operator approved 2026-10-25)

1. Done: frame-rate monitor off (sdkconfig regenerated from defaults), console quiet.
2. Done: gear button left of the Wi-Fi status, Settings screen as an lv_tabview, "Wi-Fi" tab.
3. Done (flashed, not yet tested on screen by the operator; uncommitted): Wi-Fi profiles Work / Home2 / Home3 (SSID shown, password hidden, editable, saved in
   NVS). Default passwords live in a git-ignored local header, only a template is tracked
   (the repo is public). Work is used when nothing is saved yet (after a full erase). The
   `wifi` console command stays as a backup.

4. Done (flashed 2026-10-25, uncommitted): Scan button in the Wi-Fi tab (list also logged),
   Home3 default SSID "HomeTest_3" (local header only), Wi-Fi icon green/yellow/red + slash.
   4th profile "Hotspot - Test" added (defaults in the local header only).
   Open: Home2 "homeTest" fails with reason 201 (not found); once it was briefly connected but
   HTTPS failed. Likely weak signal; waiting for the operator's scan result.

Note: the console input only works on the USB TO UART port; the USB-OTG port is output only.

## Next (operator to decide)

- JTAG debugging over the USB-OTG port.
- Moving the firmware UI to LVGL Pro exported code (needs `CONFIG_LV_USE_OBJ_NAME=y`, and
  removing `main/fonts/`; see docs/lvgl-pro-guide.md). The design project is behind the
  firmware: it lacks the banner, the bigger rows and the emoji badges.
