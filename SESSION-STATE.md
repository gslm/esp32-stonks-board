# Session state

Living checkpoint of in-flight work (see DIRECTIVES.md, rule 6). Keep it short and current.
Last updated: 2026-10-23.

## Current task: put the project under git and push to GitHub

Checkpoints agreed with the operator:

1. Done: folder layout. This folder (02_stock_watchlist) is the repo root and holds the firmware,
   `docs/`, `lvgl_pro_ui/` and `.clangd`. Deleted: `lvgl-editor-examples/`,
   `ESP32-STOCK.code-workspace`, the old root `.vscode/`.
2. Done: `.gitignore`, this file, README section "Set up a new workstation".
   No absolute paths in tracked files (operator rule): `.vscode/settings.json` is untracked,
   `.vscode/settings.example.json` is the portable, tracked version.
3. Pending: git init, first commits, push. The operator creates the empty GitHub repo and
   gives its URL. Commands are handed over as small blocks (DIRECTIVES.md rules 7, 11, 12).

## Open questions for the operator

- GitHub repo name and URL (operator creates the empty repo).

## In progress (before checkpoint 3)

- Rolling quote banner along the full bottom row of the top bar: right to left, 14 px font, a new random quote from
  money_quotes.txt (embedded at build time) each time the previous one has scrolled off,
  random colour from a palette.

## In progress: display smoothness

Step 1 (measure): LVGL perf monitor in log mode, enabled only in the local `sdkconfig` (not
tracked, not in sdkconfig.defaults). Turn it off again when the measurements are done.
Measured while the list glides:
- Baseline: 11 FPS, render 62 ms + flush 21 ms per frame.
- Step 2, two render cores (`LV_OS_FREERTOS` + `LV_DRAW_SW_DRAW_UNIT_CNT=2`, in sdkconfig.defaults): 14 FPS, render 44 ms + flush 20 ms. Done.

- Double draw buffers: not possible. esp_lvgl_port copies to the screen synchronously in the
  LVGL task, and the BSP hard-codes one buffer.
- Draw buffer height 480 (trial): 15.6 FPS, render 39 ms + flush 20 ms. Not kept.
- Anti-tearing direct mode with 2 frame buffers (in sdkconfig.defaults): 19.5 FPS, render 39 ms
  + flush 7 ms, no tearing. Kept.

- Frame-synced glide (list moves once per frame in LV_EVENT_REFR_START): done, FPS unchanged
  (19.5), motion should be more even. Rendering (39 ms) is now the limit.
- Still to do: turn the perf monitor log off again (local sdkconfig only) when the operator is done.

## Device state

- Board runs the current firmware.
- Wi-Fi credentials are stored in the board's NVS, not in the repo.
- Auto-roll speed: default 0.3x, maximum 1.0x (changed 2026-10-23, flashed and verified).

## Not started (later)

- JTAG debugging over the USB-OTG port.
- Moving the firmware UI to LVGL Pro exported code. It needs `CONFIG_LV_USE_OBJ_NAME=y` and
  the removal of `main/fonts/` (see docs/lvgl-pro-guide.md).
