#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_setup.h — WiFi / ZIP-code setup screen
//
// Layout (800 x 480):
//   Title bar at top
//   Three text fields: WiFi SSID (+ Scan button), WiFi Password, ZIP Code
//   [Connect & Save] button
//   Night brightness slider
//   Status label (shows connecting... / error / success)
//   LVGL on-screen keyboard at bottom (appears when a textarea is focused)
//   WiFi scan popup (overlay, appears after Scan button is pressed)
//
// Call ui_setup_create() once; the returned lv_obj_t* is the screen.
// Call ui_setup_set_status() to update the status label from main.cpp.
// Call ui_setup_show_scan_results() after WiFi.scanNetworks() completes.
// Call ui_setup_get_ssid() / _pass() / _zip() to read values on Connect press.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "prefs_mgr.h"

// ─── WiFi scan result (populated by main.cpp after WiFi.scanNetworks()) ──────
struct WifiNetwork {
    char ssid[33];   // max 32 chars + null
    int  rssi;
    bool secured;
};

// ─── Handles exposed to main.cpp ──────────────────────────────────────────
static lv_obj_t* _setup_scr       = nullptr;
static lv_obj_t* _ta_ssid         = nullptr;
static lv_obj_t* _ta_pass         = nullptr;
static lv_obj_t* _ta_zip          = nullptr;
static lv_obj_t* _lbl_status      = nullptr;
static lv_obj_t* _setup_keyboard  = nullptr;
static lv_obj_t* _btn_connect     = nullptr;
static lv_obj_t* _btn_scan        = nullptr;
static lv_obj_t* _setup_slider    = nullptr;  // night brightness slider
static lv_obj_t* _lbl_night_br    = nullptr;  // "Night Brightness: XX%" label
static lv_obj_t* _scan_popup      = nullptr;  // scan results overlay
static AppPrefs* _setup_prefs_ptr = nullptr;  // live prefs pointer for slider cb

static WifiNetwork _scan_results[20];
static int         _scan_count = 0;

// ─── Callback typedefs ────────────────────────────────────────────────────
typedef void (*SetupConnectCb)(const char* ssid, const char* pass, const char* zip);
typedef void (*BrightnessCb)(uint8_t night_brightness_pct);
typedef void (*ScanNetworksCb)();

static SetupConnectCb _setupConnectCb  = nullptr;
static BrightnessCb   _brightnessCb   = nullptr;
static ScanNetworksCb _scanNetworksCb  = nullptr;

// ─── Accessors ─────────────────────────────────────────────────────────────
inline const char* ui_setup_get_ssid() {
    return lv_textarea_get_text(_ta_ssid);
}
inline const char* ui_setup_get_pass() {
    return lv_textarea_get_text(_ta_pass);
}
inline const char* ui_setup_get_zip() {
    return lv_textarea_get_text(_ta_zip);
}
inline void ui_setup_set_status(const char* msg, lv_color_t color) {
    if (_lbl_status) {
        lv_label_set_text(_lbl_status, msg);
        lv_obj_set_style_text_color(_lbl_status, color, 0);
    }
}

// ─── Shared keyboard focus handler ────────────────────────────────────────
static void ta_focus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta         = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(_setup_keyboard, ta);
        if (ta == _ta_zip) {
            lv_keyboard_set_mode(_setup_keyboard, LV_KEYBOARD_MODE_NUMBER);
        } else {
            lv_keyboard_set_mode(_setup_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        }
        lv_obj_clear_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    }
    if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─── Night brightness slider handler ──────────────────────────────────────
static void _slider_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    int val = lv_slider_get_value(lv_event_get_target(e));

    char buf[32];
    snprintf(buf, sizeof(buf), "Night Brightness: %d%%", val);
    if (_lbl_night_br) lv_label_set_text(_lbl_night_br, buf);

    if (_setup_prefs_ptr) {
        _setup_prefs_ptr->night_brightness = (uint8_t)val;
        prefs_save_brightness((uint8_t)val);
    }
    if (_brightnessCb) _brightnessCb((uint8_t)val);
}

// ─── Connect button handler ────────────────────────────────────────────────
static void connect_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && _setupConnectCb) {
        _setupConnectCb(ui_setup_get_ssid(),
                        ui_setup_get_pass(),
                        ui_setup_get_zip());
    }
}

// ─── Scan popup: network selected ─────────────────────────────────────────
static void _network_select_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < _scan_count && _ta_ssid) {
        lv_textarea_set_text(_ta_ssid, _scan_results[idx].ssid);
    }
    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }
    ui_setup_set_status("Network selected. Enter password and tap Connect.",
                        lv_color_hex(0x4fc3f7));
}

// ─── Scan popup: cancel ────────────────────────────────────────────────────
static void _scan_popup_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }
    ui_setup_set_status("Enter your WiFi credentials and ZIP code.",
                        lv_color_hex(0xaaaaaa));
}

// ─── Scan button handler ───────────────────────────────────────────────────
static void _scan_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (_scanNetworksCb) _scanNetworksCb();
}

// ─── Public: populate and show scan results popup ─────────────────────────
// Called by main.cpp after WiFi.scanNetworks() completes.
inline void ui_setup_show_scan_results(const WifiNetwork* networks, int count) {
    _scan_count = (count > 20) ? 20 : count;
    for (int i = 0; i < _scan_count; i++) _scan_results[i] = networks[i];

    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }

    // Popup panel — centered over the setup screen
    _scan_popup = lv_obj_create(_setup_scr);
    lv_obj_set_size(_scan_popup, 700, 340);
    lv_obj_align(_scan_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_scan_popup, lv_color_hex(0x1e2d4a), 0);
    lv_obj_set_style_border_color(_scan_popup, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_border_width(_scan_popup, 2, 0);
    lv_obj_set_style_radius(_scan_popup, 10, 0);
    lv_obj_set_style_pad_all(_scan_popup, 8, 0);
    lv_obj_clear_flag(_scan_popup, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* ptitle = lv_label_create(_scan_popup);
    lv_label_set_text(ptitle, "Select WiFi Network");
    lv_obj_set_style_text_font(ptitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ptitle, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(ptitle, LV_ALIGN_TOP_MID, 0, 6);

    // Network list
    lv_obj_t* list = lv_list_create(_scan_popup);
    lv_obj_set_size(list, 668, 240);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 38);

    if (_scan_count == 0) {
        lv_list_add_text(list, "No networks found");
    } else {
        for (int i = 0; i < _scan_count; i++) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%s  (%d dBm)%s",
                     _scan_results[i].ssid,
                     _scan_results[i].rssi,
                     _scan_results[i].secured ? "  [Secured]" : "");
            lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
            lv_obj_add_event_cb(btn, _network_select_cb, LV_EVENT_CLICKED,
                                (void*)(intptr_t)i);
        }
    }

    // Cancel button
    lv_obj_t* cancel_btn = lv_btn_create(_scan_popup);
    lv_obj_set_size(cancel_btn, 120, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, _scan_popup_close_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_lbl);
}

// ─── Helper: create a labeled textarea pair ────────────────────────────────
// width defaults to full (680 px); pass a narrower value to leave room for buttons.
static lv_obj_t* make_labeled_ta(lv_obj_t* parent, const char* label_text,
                                  int y, bool password_mode, bool numbers_only,
                                  int width = 680) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl, 60, y);

    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, width, 48);
    lv_obj_set_pos(ta, 60, y + 26);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, password_mode);
    if (numbers_only) {
        lv_textarea_set_accepted_chars(ta, "0123456789");
        lv_textarea_set_max_length(ta, 5);
    } else {
        lv_textarea_set_max_length(ta, 63);
    }
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_ALL, nullptr);
    return ta;
}

// ─── Public: create and return the setup screen ───────────────────────────
inline lv_obj_t* ui_setup_create(AppPrefs* prefs, SetupConnectCb connectCb,
                                   BrightnessCb brightCb   = nullptr,
                                   ScanNetworksCb scanCb   = nullptr) {
    _setupConnectCb  = connectCb;
    _brightnessCb    = brightCb;
    _scanNetworksCb  = scanCb;
    _setup_prefs_ptr = prefs;

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _setup_scr = scr;

    // ── Title bar ──────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Smart Clock Setup");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    // Horizontal divider under title
    lv_obj_t* div = lv_obj_create(scr);
    lv_obj_set_size(div, 760, 2);
    lv_obj_set_pos(div, 20, 58);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // ── SSID field (narrowed to 552 px to leave room for the Scan button) ──
    _ta_ssid = make_labeled_ta(scr, "WiFi Network (SSID):", 72, false, false, 552);

    // Scan WiFi button — right of SSID textarea
    _btn_scan = lv_btn_create(scr);
    lv_obj_set_size(_btn_scan, 118, 52);
    lv_obj_set_pos(_btn_scan, 622, 95);
    lv_obj_set_style_bg_color(_btn_scan, lv_color_hex(0x1565c0), 0);
    lv_obj_set_style_radius(_btn_scan, 8, 0);
    lv_obj_add_event_cb(_btn_scan, _scan_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* scan_lbl = lv_label_create(_btn_scan);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(scan_lbl);

    // ── Password and ZIP fields (full width) ───────────────────────────────
    _ta_pass = make_labeled_ta(scr, "WiFi Password:",  156, true,  false);
    _ta_zip  = make_labeled_ta(scr, "Zip Code:",       240, false, true);

    // Pre-fill saved values
    if (prefs) {
        if (strlen(prefs->wifi_ssid) > 0) lv_textarea_set_text(_ta_ssid, prefs->wifi_ssid);
        if (strlen(prefs->wifi_pass) > 0) lv_textarea_set_text(_ta_pass, prefs->wifi_pass);
        if (strlen(prefs->zip_code)  > 0) lv_textarea_set_text(_ta_zip,  prefs->zip_code);
    }

    // ── Connect button ─────────────────────────────────────────────────────
    _btn_connect = lv_btn_create(scr);
    lv_obj_set_size(_btn_connect, 240, 50);
    lv_obj_set_pos(_btn_connect, 280, 308);
    lv_obj_set_style_bg_color(_btn_connect, lv_color_hex(0x0288d1), 0);
    lv_obj_set_style_radius(_btn_connect, 8, 0);
    lv_obj_add_event_cb(_btn_connect, connect_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_lbl = lv_label_create(_btn_connect);
    lv_label_set_text(btn_lbl, "Connect & Save");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_18, 0);
    lv_obj_center(btn_lbl);

    // ── Night brightness slider ────────────────────────────────────────────
    uint8_t init_br = (prefs ? prefs->night_brightness : 50);
    char br_buf[32];
    snprintf(br_buf, sizeof(br_buf), "Night Brightness: %d%%", (int)init_br);

    _lbl_night_br = lv_label_create(scr);
    lv_label_set_text(_lbl_night_br, br_buf);
    lv_obj_set_style_text_font(_lbl_night_br, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_night_br, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_pos(_lbl_night_br, 60, 366);

    _setup_slider = lv_slider_create(scr);
    lv_obj_set_size(_setup_slider, 680, 18);
    lv_obj_set_pos(_setup_slider, 60, 392);
    lv_slider_set_range(_setup_slider, 10, 100);
    lv_slider_set_value(_setup_slider, (int)init_br, LV_ANIM_OFF);
    lv_obj_add_event_cb(_setup_slider, _slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_set_style_bg_color(_setup_slider, lv_color_hex(0x0288d1),
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(_setup_slider, lv_color_hex(0x4fc3f7),
                               LV_PART_KNOB     | LV_STATE_DEFAULT);

    // ── Status label ───────────────────────────────────────────────────────
    _lbl_status = lv_label_create(scr);
    lv_label_set_text(_lbl_status, "Enter your WiFi credentials and ZIP code");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_width(_lbl_status, 720);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 430);
    lv_label_set_long_mode(_lbl_status, LV_LABEL_LONG_WRAP);

    // ── On-screen keyboard (hidden until textarea is focused) ──────────────
    _setup_keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(_setup_keyboard, SCREEN_WIDTH, 200);
    lv_obj_align(_setup_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(_setup_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);

    return scr;
}

// ─── Pre-fill fields and reset status (call when returning to setup screen)
inline void ui_setup_refresh(AppPrefs* prefs) {
    if (!_setup_scr) return;
    if (prefs) {
        _setup_prefs_ptr = prefs;
        if (strlen(prefs->wifi_ssid) > 0) lv_textarea_set_text(_ta_ssid, prefs->wifi_ssid);
        if (strlen(prefs->zip_code)  > 0) lv_textarea_set_text(_ta_zip,  prefs->zip_code);
        if (_setup_slider) {
            lv_slider_set_value(_setup_slider, (int)prefs->night_brightness, LV_ANIM_OFF);
            char buf[32];
            snprintf(buf, sizeof(buf), "Night Brightness: %d%%",
                     (int)prefs->night_brightness);
            if (_lbl_night_br) lv_label_set_text(_lbl_night_br, buf);
        }
    }
    ui_setup_set_status("Enter your WiFi credentials and ZIP code.",
                        lv_color_hex(0xaaaaaa));
    lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}
