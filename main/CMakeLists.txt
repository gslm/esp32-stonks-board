idf_component_register(
    SRCS
        "main.c"
        "stock_model.c"
        "settings.c"
        "wifi.c"
        "quotes.c"
        "cli.c"
        "screen.c"
        "banner_messages.c"
        "ui/ui.c"
        "ui/ui_theme.c"
        "ui/ui_watchlist.c"
        "ui/ui_detail.c"
        "ui/ui_banner.c"
        "ui/ui_settings.c"
        "images/img_chart_up.c"
        "images/img_chart_down.c"
        "images/img_moneybag.c"
        "images/img_money_wings.c"
        "images/img_tophat.c"
        "images/img_lightning.c"
        "fonts/font_12.c"
        "fonts/font_14.c"
        "fonts/font_16.c"
        "fonts/font_18.c"
        "fonts/font_20.c"
        "fonts/font_24.c"
        "fonts/font_40.c"
    INCLUDE_DIRS "." "ui"
    EMBED_TXTFILES "../money_quotes.txt" "../circuit_breaker_quotes.txt")

# Images made by LVGL's LVGLImage.py include "lvgl.h" only when this is defined
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
