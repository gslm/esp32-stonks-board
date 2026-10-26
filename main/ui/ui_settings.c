/*
 * Settings screen, opened with the gear button on the main screen. A tab view, so new topics
 * (battery, stocks, ...) can be added later as more tabs. Today: one "Wi-Fi" tab with the
 * connection status, four profiles (Work, Home2, Home3, Hotspot - Test): tap one to connect, Edit to change it,
 * and a scan of nearby networks to check what the board can actually see.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "stock_model.h"
#include "ui_screens.h"
#include "ui_theme.h"
#include "wifi.h"

#define STATUS_REFRESH_MS 500
#define PROFILE_ROW_H     64

enum { INFO_STATUS, INFO_IP, INFO_SIGNAL, INFO_COUNT };

typedef struct {
    lv_obj_t *row;
    lv_obj_t *name;
    lv_obj_t *ssid;
} profile_row_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_info[INFO_COUNT];
static profile_row_t s_profiles[SETTINGS_WIFI_PROFILES];

/* Scan of nearby networks */
static lv_obj_t *s_scan_status;
static lv_obj_t *s_scan_list;
static uint32_t s_scan_version_shown;

/* Edit overlay (on the top layer, only while editing) */
static lv_obj_t *s_edit;
static lv_obj_t *s_edit_ssid;
static lv_obj_t *s_edit_password;
static lv_obj_t *s_edit_hint;
static int s_edit_profile;

static void update_wifi_tab(void);

static void on_back_clicked(lv_event_t *e)
{
    lv_screen_load(ui_watchlist_screen());
}

static void create_header(void)
{
    lv_obj_t *header = ui_box_create(s_screen);
    lv_obj_set_size(header, 480, UI_HEADER_H);
    lv_obj_set_style_bg_color(header, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = ui_label_create(back, &font_20, UI_TEXT_PRIMARY);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_center(arrow);

    lv_obj_t *title = ui_label_create(header, &font_24, UI_TEXT_PRIMARY);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 60, 0);
}

/* ---------------------------------------------------------------------------------------------- */
/* Edit overlay                                                                                    */
/* ---------------------------------------------------------------------------------------------- */

static void close_edit(void)
{
    if (s_edit) {
        lv_obj_delete_async(s_edit);
        s_edit = NULL;
    }
}

static void save_edit(void)
{
    const char *ssid = lv_textarea_get_text(s_edit_ssid);
    const char *password = lv_textarea_get_text(s_edit_password);
    if (ssid[0] == '\0') {
        ui_set_text(s_edit_hint, "The network name can't be empty");
        lv_obj_set_style_text_color(s_edit_hint, UI_DOWN, 0);
        return;
    }
    char old_ssid[SETTINGS_SSID_LEN];
    char old_password[SETTINGS_PASSWORD_LEN];
    settings_get_wifi_profile(s_edit_profile, old_ssid, old_password);
    /* An empty password field keeps the saved password */
    settings_set_wifi_profile(s_edit_profile, ssid, password[0] ? password : old_password);
    if (s_edit_profile == settings_get_wifi_active()) {
        wifi_use_profile(s_edit_profile); /* reconnect with the new credentials */
    }
    close_edit();
    update_wifi_tab();
}

static void on_keyboard_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        save_edit();
    } else if (code == LV_EVENT_CANCEL) {
        close_edit();
    }
}

/* Tapping a field sends the keyboard to it */
static void on_field_focused(lv_event_t *e)
{
    lv_keyboard_set_textarea(lv_event_get_user_data(e), lv_event_get_target_obj(e));
}

static lv_obj_t *create_field(lv_obj_t *card, int32_t y, const char *placeholder, lv_obj_t *kb)
{
    lv_obj_t *ta = lv_textarea_create(card);
    lv_obj_set_size(ta, 416, 44);
    lv_obj_set_pos(ta, 0, y);
    lv_obj_set_style_text_font(ta, &font_16, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_add_event_cb(ta, on_field_focused, LV_EVENT_FOCUSED, kb);
    return ta;
}

static void open_edit(int profile)
{
    if (s_edit) {
        return;
    }
    s_edit_profile = profile;
    char ssid[SETTINGS_SSID_LEN];
    char password[SETTINGS_PASSWORD_LEN];
    settings_get_wifi_profile(profile, ssid, password);

    /* Dimmed full-screen backdrop; being clickable, it blocks taps on the screen behind */
    s_edit = ui_box_create(lv_layer_top());
    lv_obj_set_size(s_edit, 480, 480);
    lv_obj_add_flag(s_edit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_edit, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_edit, LV_OPA_60, 0);

    lv_obj_t *card = ui_box_create(s_edit);
    lv_obj_set_size(card, 448, 216);
    lv_obj_set_pos(card, 16, 12);
    lv_obj_set_style_bg_color(card, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);

    char title[40];
    snprintf(title, sizeof(title), "Edit profile \"%s\"", settings_wifi_profile_name(profile));
    lv_obj_t *t = ui_label_create(card, &font_20, UI_TEXT_PRIMARY);
    lv_label_set_text(t, title);

    lv_obj_t *kb = lv_keyboard_create(s_edit);
    lv_obj_set_size(kb, 480, 240);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &font_20, 0);
    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_ALL, NULL);

    s_edit_ssid = create_field(card, 36, "Network name (SSID)", kb);
    lv_textarea_set_max_length(s_edit_ssid, SETTINGS_SSID_LEN - 1);
    lv_textarea_set_text(s_edit_ssid, ssid);

    s_edit_password = create_field(card, 88, "Password (empty = keep current)", kb);
    lv_textarea_set_max_length(s_edit_password, SETTINGS_PASSWORD_LEN - 1);
    lv_textarea_set_password_mode(s_edit_password, true);

    s_edit_hint = ui_label_create(card, &font_14, UI_TEXT_SECONDARY);
    lv_obj_set_pos(s_edit_hint, 0, 144);
    lv_label_set_text(s_edit_hint, "Tap a field to type. OK saves, keyboard key cancels.");

    lv_keyboard_set_textarea(kb, s_edit_ssid);
    lv_obj_add_state(s_edit_ssid, LV_STATE_FOCUSED);
}

/* ---------------------------------------------------------------------------------------------- */
/* Wi-Fi tab                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

static void on_profile_clicked(lv_event_t *e)
{
    wifi_use_profile((int)(intptr_t)lv_event_get_user_data(e));
    update_wifi_tab();
}

static void on_edit_clicked(lv_event_t *e)
{
    open_edit((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_scan_clicked(lv_event_t *e)
{
    if (wifi_scan_start() == ESP_OK) {
        ui_set_text(s_scan_status, "Scanning... (a few seconds)");
    }
}

/* Profile name whose SSID matches, or NULL */
static const char *profile_for_ssid(const char *ssid)
{
    char p_ssid[SETTINGS_SSID_LEN];
    char p_password[SETTINGS_PASSWORD_LEN];
    for (int i = 0; i < SETTINGS_WIFI_PROFILES; i++) {
        settings_get_wifi_profile(i, p_ssid, p_password);
        if (p_ssid[0] && strcmp(p_ssid, ssid) == 0) {
            return settings_wifi_profile_name(i);
        }
    }
    return NULL;
}

static void update_scan(void)
{
    static wifi_network_t nets[WIFI_SCAN_MAX];
    uint32_t version;
    int n = wifi_scan_results(nets, WIFI_SCAN_MAX, &version);
    if (wifi_scan_running()) {
        ui_set_text(s_scan_status, "Scanning... (a few seconds)");
        return;
    }
    if (version == s_scan_version_shown) {
        return;
    }
    s_scan_version_shown = version;

    char buf[80];
    snprintf(buf, sizeof(buf), "%d networks found (2.4 GHz only), strongest first", n);
    ui_set_text(s_scan_status, buf);
    lv_obj_clean(s_scan_list);
    for (int i = 0; i < n; i++) {
        const char *profile = profile_for_ssid(nets[i].ssid);
        snprintf(buf, sizeof(buf), "%.32s%s%s%s", nets[i].ssid[0] ? nets[i].ssid : "(hidden)",
                 profile ? "  (" : "", profile ? profile : "", profile ? ")" : "");
        lv_obj_t *row = ui_box_create(s_scan_list);
        lv_obj_set_size(row, LV_PCT(100), 26);
        lv_obj_t *name = ui_label_create(row, &font_14, profile ? UI_ACCENT : UI_TEXT_PRIMARY);
        ui_label_max_lines(name, 300, 1);
        lv_label_set_text(name, buf);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        snprintf(buf, sizeof(buf), "%d dBm  ch %d", nets[i].rssi, nets[i].channel);
        lv_obj_t *info = ui_label_create(row, &font_14, UI_TEXT_SECONDARY);
        lv_label_set_text(info, buf);
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void create_scan_section(lv_obj_t *tab)
{
    lv_obj_t *btn = lv_button_create(tab);
    lv_obj_set_size(btn, 200, 40);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, on_scan_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = ui_label_create(btn, &font_16, lv_color_white());
    lv_label_set_text(label, LV_SYMBOL_REFRESH "  Scan networks");
    lv_obj_center(label);

    s_scan_status = ui_label_create(tab, &font_14, UI_TEXT_SECONDARY);
    lv_label_set_text(s_scan_status, "Lists the networks the board can see, with signal strength");

    s_scan_list = ui_box_create(tab);
    lv_obj_set_size(s_scan_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_scan_list, LV_FLEX_FLOW_COLUMN);
}

/* One "caption: value" line of the status block */
static lv_obj_t *create_info_row(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *row = ui_box_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 28);
    lv_obj_t *cap = ui_label_create(row, &font_16, UI_TEXT_SECONDARY);
    lv_label_set_text(cap, caption);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value = ui_label_create(row, &font_16, UI_TEXT_PRIMARY);
    lv_label_set_text(value, "--");
    lv_obj_align(value, LV_ALIGN_LEFT_MID, 120, 0);
    return value;
}

static void create_profile_row(lv_obj_t *parent, int i)
{
    profile_row_t *p = &s_profiles[i];
    p->row = ui_box_create(parent);
    lv_obj_set_size(p->row, LV_PCT(100), PROFILE_ROW_H);
    lv_obj_add_flag(p->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(p->row, 8, 0);
    lv_obj_set_style_bg_opa(p->row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p->row, UI_BG_HEADER, 0);
    lv_obj_set_style_bg_color(p->row, UI_BG_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(p->row, UI_ACCENT, 0);
    lv_obj_add_event_cb(p->row, on_profile_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    p->name = ui_label_create(p->row, &font_20, UI_TEXT_PRIMARY);
    lv_obj_set_pos(p->name, 14, 8);
    p->ssid = ui_label_create(p->row, &font_14, UI_TEXT_SECONDARY);
    lv_obj_set_pos(p->ssid, 14, 36);
    ui_label_max_lines(p->ssid, 280, 1);

    lv_obj_t *edit = lv_button_create(p->row);
    lv_obj_set_size(edit, 84, 40);
    lv_obj_align(edit, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_shadow_width(edit, 0, 0);
    lv_obj_add_event_cb(edit, on_edit_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *label = ui_label_create(edit, &font_16, lv_color_white());
    lv_label_set_text(label, "Edit");
    lv_obj_center(label);
}

static void create_wifi_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_row(tab, 6, 0);

    static const char *const CAPTIONS[INFO_COUNT] = {"Status", "IP address", "Signal"};
    for (int i = 0; i < INFO_COUNT; i++) {
        s_info[i] = create_info_row(tab, CAPTIONS[i]);
    }

    lv_obj_t *caption = ui_label_create(tab, &font_14, UI_TEXT_SECONDARY);
    lv_label_set_text(caption, "Profiles: tap one to connect");
    lv_obj_set_style_pad_top(caption, 8, 0);

    for (int i = 0; i < SETTINGS_WIFI_PROFILES; i++) {
        create_profile_row(tab, i);
    }

    create_scan_section(tab);
}

static void update_wifi_tab(void)
{
    char ssid[SETTINGS_SSID_LEN];
    char password[SETTINGS_PASSWORD_LEN];
    char buf[64];
    app_status_t st;
    model_get_status(&st);
    int active = settings_get_wifi_active();

    settings_get_wifi_profile(active, ssid, password);
    static const char *const STATE[] = {"Not configured", "Connecting to", "Connected to"};
    snprintf(buf, sizeof(buf), "%s %s", STATE[st.net], st.net == NET_NOT_CONFIGURED ? "" : ssid);
    ui_set_text(s_info[INFO_STATUS], buf);
    lv_obj_set_style_text_color(s_info[INFO_STATUS], st.net == NET_ONLINE ? UI_UP : UI_TEXT_PRIMARY, 0);

    wifi_ip_string(buf, sizeof(buf));
    ui_set_text(s_info[INFO_IP], buf[0] ? buf : "--");

    int rssi = wifi_rssi();
    if (rssi != 0) {
        snprintf(buf, sizeof(buf), "%d dBm", rssi);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    ui_set_text(s_info[INFO_SIGNAL], buf);

    for (int i = 0; i < SETTINGS_WIFI_PROFILES; i++) {
        profile_row_t *p = &s_profiles[i];
        settings_get_wifi_profile(i, ssid, password);
        snprintf(buf, sizeof(buf), "%s%s", settings_wifi_profile_name(i), i == active ? "  " LV_SYMBOL_OK : "");
        ui_set_text(p->name, buf);
        lv_obj_set_style_text_color(p->name, i == active ? UI_ACCENT : UI_TEXT_PRIMARY, 0);
        ui_set_text(p->ssid, ssid[0] ? ssid : "(no network set)");
        lv_obj_set_style_border_width(p->row, i == active ? 2 : 0, 0);
    }
}

static void status_tick(lv_timer_t *timer)
{
    if (lv_screen_active() == s_screen && s_edit == NULL) {
        update_wifi_tab();
        update_scan();
    }
}

void ui_settings_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, UI_BG_SCREEN, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header();

    lv_obj_t *tabs = lv_tabview_create(s_screen);
    lv_obj_set_size(tabs, 480, 480 - UI_HEADER_H);
    lv_obj_set_pos(tabs, 0, UI_HEADER_H);
    lv_obj_set_style_bg_color(tabs, UI_BG_SCREEN, 0);
    lv_tabview_set_tab_bar_position(tabs, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabs, 44);
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabs);
    lv_obj_set_style_bg_color(bar, UI_BG_HEADER, 0);
    lv_obj_set_style_text_font(bar, &font_16, 0);

    /* New settings topics: add a tab here, with its own create/update functions */
    create_wifi_tab(lv_tabview_add_tab(tabs, LV_SYMBOL_WIFI " Wi-Fi"));

    lv_timer_create(status_tick, STATUS_REFRESH_MS, NULL);
}

void ui_settings_open(void)
{
    update_wifi_tab();
    lv_screen_load(s_screen);
}
