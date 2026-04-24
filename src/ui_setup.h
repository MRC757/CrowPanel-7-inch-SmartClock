#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_setup.h — WiFi / ZIP setup screen + Stocks & Teams configuration
//
// Layout (800 x 480):
//   lv_tabview (fills screen, tab bar 44 px at top)
//   ├─ Tab 1 "WiFi & Location": SSID, Password, ZIP, Connect button,
//   │                           Night brightness slider, Status label
//   └─ Tab 2 "Stocks & Teams":  6 stock rows (symbol + display name),
//                               NBA Team 1 dropdown, NBA Team 2 dropdown
//   Shared on-screen keyboard (child of scr, overlays all tabs)
//   WiFi scan popup (overlay on scr, above tabview)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "prefs_mgr.h"

// ─── WiFi scan result ─────────────────────────────────────────────────────────
struct WifiNetwork {
    char ssid[33];
    int  rssi;
    bool secured;
};

// ─── NBA team table (Ball Don't Lie IDs, alphabetical by city) ───────────────
static const struct { int id; const char* name; } _NBA_TEAMS[] = {
    {  1, "Atlanta Hawks"         },
    {  2, "Boston Celtics"        },
    {  3, "Brooklyn Nets"         },
    {  4, "Charlotte Hornets"     },
    {  5, "Chicago Bulls"         },
    {  6, "Cleveland Cavaliers"   },
    {  7, "Dallas Mavericks"      },
    {  8, "Denver Nuggets"        },
    {  9, "Detroit Pistons"       },
    { 10, "Golden State Warriors" },
    { 11, "Houston Rockets"       },
    { 12, "Indiana Pacers"        },
    { 13, "LA Clippers"           },
    { 14, "LA Lakers"             },
    { 15, "Memphis Grizzlies"     },
    { 16, "Miami Heat"            },
    { 17, "Milwaukee Bucks"       },
    { 18, "Minnesota Timberwolves"},
    { 19, "New Orleans Pelicans"  },
    { 20, "New York Knicks"       },
    { 21, "Oklahoma City Thunder" },
    { 22, "Orlando Magic"         },
    { 23, "Philadelphia 76ers"    },
    { 24, "Phoenix Suns"          },
    { 25, "Portland Trail Blazers"},
    { 26, "Sacramento Kings"      },
    { 27, "San Antonio Spurs"     },
    { 28, "Toronto Raptors"       },
    { 29, "Utah Jazz"             },
    { 30, "Washington Wizards"    },
};
static const int _NBA_TEAM_COUNT = 30;

// ─── Handles ──────────────────────────────────────────────────────────────────
static lv_obj_t* _setup_scr       = nullptr;
static lv_obj_t* _ta_ssid         = nullptr;
static lv_obj_t* _ta_pass         = nullptr;
static lv_obj_t* _ta_zip          = nullptr;
static lv_obj_t* _lbl_status      = nullptr;
static lv_obj_t* _setup_keyboard  = nullptr;
static lv_obj_t* _btn_connect     = nullptr;
static lv_obj_t* _btn_scan        = nullptr;
static lv_obj_t* _setup_slider    = nullptr;
static lv_obj_t* _lbl_night_br    = nullptr;
static lv_obj_t* _scan_popup      = nullptr;
static lv_obj_t* _ta_stock_sym[STOCK_COUNT] = {};
static lv_obj_t* _ta_stock_nam[STOCK_COUNT] = {};
static lv_obj_t* _dd_nba[2]       = {};
static AppPrefs* _setup_prefs_ptr = nullptr;

static WifiNetwork _scan_results[20];
static int         _scan_count = 0;

// ─── Callback typedefs ────────────────────────────────────────────────────────
typedef void (*SetupConnectCb)(const char* ssid, const char* pass, const char* zip);
typedef void (*BrightnessCb)(uint8_t night_brightness_pct);
typedef void (*ScanNetworksCb)();
typedef void (*StocksChangedCb)(int idx, const char* symbol, const char* name);
typedef void (*TeamsChangedCb)(int team1_id, int team2_id);

static SetupConnectCb  _setupConnectCb  = nullptr;
static BrightnessCb    _brightnessCb    = nullptr;
static ScanNetworksCb  _scanNetworksCb  = nullptr;
static StocksChangedCb _stocksChangedCb = nullptr;
static TeamsChangedCb  _teamsChangedCb  = nullptr;

// ─── Accessors ────────────────────────────────────────────────────────────────
inline const char* ui_setup_get_ssid() { return lv_textarea_get_text(_ta_ssid); }
inline const char* ui_setup_get_pass() { return lv_textarea_get_text(_ta_pass); }
inline const char* ui_setup_get_zip()  { return lv_textarea_get_text(_ta_zip);  }

inline void ui_setup_set_status(const char* msg, lv_color_t color) {
    if (_lbl_status) {
        lv_label_set_text(_lbl_status, msg);
        lv_obj_set_style_text_color(_lbl_status, color, 0);
    }
}

// ─── Helper: find dropdown index for a given NBA team ID ─────────────────────
static int _nba_team_index(int team_id) {
    for (int i = 0; i < _NBA_TEAM_COUNT; i++) {
        if (_NBA_TEAMS[i].id == team_id) return i;
    }
    return 0;
}

// ─── Keyboard focus handler (WiFi / ZIP textareas) ───────────────────────────
static void ta_focus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta         = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(_setup_keyboard, ta);
        lv_keyboard_set_mode(_setup_keyboard,
            (ta == _ta_zip) ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    }
    if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─── Keyboard focus handler (stock textareas) ─────────────────────────────────
// user_data = stock index (0–5); fires save on defocus.
static void _stock_ta_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta         = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(_setup_keyboard, ta);
        lv_keyboard_set_mode(_setup_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
        lv_obj_clear_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    }
    if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (_stocksChangedCb && idx >= 0 && idx < STOCK_COUNT && _setup_prefs_ptr) {
            const char* sym  = lv_textarea_get_text(_ta_stock_sym[idx]);
            const char* name = lv_textarea_get_text(_ta_stock_nam[idx]);
            if (strlen(sym) > 0) {
                strncpy(_setup_prefs_ptr->stock_symbols[idx], sym,  sizeof(_setup_prefs_ptr->stock_symbols[idx]) - 1);
                strncpy(_setup_prefs_ptr->stock_names[idx],   name, sizeof(_setup_prefs_ptr->stock_names[idx])   - 1);
                _stocksChangedCb(idx, sym, name);
            }
        }
    }
}

// ─── NBA dropdown change handler ──────────────────────────────────────────────
static void _team_dd_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    if (!_teamsChangedCb || !_setup_prefs_ptr) return;
    int idx1 = lv_dropdown_get_selected(_dd_nba[0]);
    int idx2 = lv_dropdown_get_selected(_dd_nba[1]);
    int id1  = _NBA_TEAMS[idx1].id;
    int id2  = _NBA_TEAMS[idx2].id;
    _setup_prefs_ptr->nba_team1_id = id1;
    _setup_prefs_ptr->nba_team2_id = id2;
    _teamsChangedCb(id1, id2);
}

// ─── Night brightness slider handler ─────────────────────────────────────────
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

// ─── Connect button handler ───────────────────────────────────────────────────
static void connect_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && _setupConnectCb)
        _setupConnectCb(ui_setup_get_ssid(), ui_setup_get_pass(), ui_setup_get_zip());
}

// ─── Scan popup handlers ──────────────────────────────────────────────────────
static void _network_select_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < _scan_count && _ta_ssid)
        lv_textarea_set_text(_ta_ssid, _scan_results[idx].ssid);
    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }
    ui_setup_set_status("Network selected. Enter password and tap Connect.",
                        lv_color_hex(0x4fc3f7));
}

static void _scan_popup_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }
    ui_setup_set_status("Enter your WiFi credentials and ZIP code.",
                        lv_color_hex(0xaaaaaa));
}

static void _scan_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (_scanNetworksCb) _scanNetworksCb();
}

// ─── Public: populate and show scan results popup ─────────────────────────────
inline void ui_setup_show_scan_results(const WifiNetwork* networks, int count) {
    _scan_count = (count > 20) ? 20 : count;
    for (int i = 0; i < _scan_count; i++) _scan_results[i] = networks[i];

    if (_scan_popup) { lv_obj_del(_scan_popup); _scan_popup = nullptr; }

    _scan_popup = lv_obj_create(_setup_scr);
    lv_obj_set_size(_scan_popup, 700, 340);
    lv_obj_align(_scan_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_scan_popup, lv_color_hex(0x1e2d4a), 0);
    lv_obj_set_style_border_color(_scan_popup, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_style_border_width(_scan_popup, 2, 0);
    lv_obj_set_style_radius(_scan_popup, 10, 0);
    lv_obj_set_style_pad_all(_scan_popup, 8, 0);
    lv_obj_clear_flag(_scan_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ptitle = lv_label_create(_scan_popup);
    lv_label_set_text(ptitle, "Select WiFi Network");
    lv_obj_set_style_text_font(ptitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ptitle, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(ptitle, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t* list = lv_list_create(_scan_popup);
    lv_obj_set_size(list, 668, 240);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 38);

    if (_scan_count == 0) {
        lv_list_add_text(list, "No networks found");
    } else {
        for (int i = 0; i < _scan_count; i++) {
            char buf[80];
            snprintf(buf, sizeof(buf), "%s  (%d dBm)%s",
                     _scan_results[i].ssid, _scan_results[i].rssi,
                     _scan_results[i].secured ? "  [Secured]" : "");
            lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, buf);
            lv_obj_add_event_cb(btn, _network_select_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
    }

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

// ─── Helper: labeled textarea ─────────────────────────────────────────────────
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

// ─── Public: create and return the setup screen ───────────────────────────────
inline lv_obj_t* ui_setup_create(AppPrefs* prefs,
                                   SetupConnectCb  connectCb,
                                   BrightnessCb    brightCb   = nullptr,
                                   ScanNetworksCb  scanCb     = nullptr,
                                   StocksChangedCb stocksCb   = nullptr,
                                   TeamsChangedCb  teamsCb    = nullptr) {
    _setupConnectCb  = connectCb;
    _brightnessCb    = brightCb;
    _scanNetworksCb  = scanCb;
    _stocksChangedCb = stocksCb;
    _teamsChangedCb  = teamsCb;
    _setup_prefs_ptr = prefs;

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _setup_scr = scr;

    // ── Tabview (fills screen; tab bar 44 px at top) ──────────────────────
    lv_obj_t* tv = lv_tabview_create(scr, LV_DIR_TOP, 44);
    lv_obj_set_size(tv, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x1a1a2e), 0);

    // Style the tab buttons bar
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tv);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x0d1b2e), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xaaaaaa), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0x4fc3f7), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_btns, lv_color_hex(0x4fc3f7), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_16, LV_PART_ITEMS);

    lv_obj_t* tab1 = lv_tabview_add_tab(tv, LV_SYMBOL_WIFI "  WiFi & Location");
    lv_obj_t* tab2 = lv_tabview_add_tab(tv, LV_SYMBOL_EDIT "  Stocks & Teams");

    // Remove default padding so absolute positions work as-is
    lv_obj_set_style_pad_all(tab1, 0, 0);
    lv_obj_set_style_pad_all(tab2, 0, 0);
    lv_obj_set_style_bg_color(tab1, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_color(tab2, lv_color_hex(0x1a1a2e), 0);

    // ════════════════════════════════════════════════════════════════════════
    // TAB 1 — WiFi & Location
    // ════════════════════════════════════════════════════════════════════════

    // ── SSID field ────────────────────────────────────────────────────────
    _ta_ssid = make_labeled_ta(tab1, "WiFi Network (SSID):", 28, false, false, 552);

    // Scan WiFi button
    _btn_scan = lv_btn_create(tab1);
    lv_obj_set_size(_btn_scan, 118, 52);
    lv_obj_set_pos(_btn_scan, 622, 51);
    lv_obj_set_style_bg_color(_btn_scan, lv_color_hex(0x1565c0), 0);
    lv_obj_set_style_radius(_btn_scan, 8, 0);
    lv_obj_add_event_cb(_btn_scan, _scan_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* scan_lbl = lv_label_create(_btn_scan);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(scan_lbl);

    // ── Password and ZIP fields ────────────────────────────────────────────
    _ta_pass = make_labeled_ta(tab1, "WiFi Password:",  112, true,  false);
    _ta_zip  = make_labeled_ta(tab1, "Zip Code:",       196, false, true);

    // Pre-fill saved values
    if (prefs) {
        if (strlen(prefs->wifi_ssid) > 0) lv_textarea_set_text(_ta_ssid, prefs->wifi_ssid);
        if (strlen(prefs->wifi_pass) > 0) lv_textarea_set_text(_ta_pass, prefs->wifi_pass);
        if (strlen(prefs->zip_code)  > 0) lv_textarea_set_text(_ta_zip,  prefs->zip_code);
    }

    // ── Connect button ─────────────────────────────────────────────────────
    _btn_connect = lv_btn_create(tab1);
    lv_obj_set_size(_btn_connect, 240, 50);
    lv_obj_set_pos(_btn_connect, 280, 264);
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

    _lbl_night_br = lv_label_create(tab1);
    lv_label_set_text(_lbl_night_br, br_buf);
    lv_obj_set_style_text_font(_lbl_night_br, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_night_br, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_pos(_lbl_night_br, 60, 322);

    _setup_slider = lv_slider_create(tab1);
    lv_obj_set_size(_setup_slider, 680, 18);
    lv_obj_set_pos(_setup_slider, 60, 348);
    lv_slider_set_range(_setup_slider, 10, 100);
    lv_slider_set_value(_setup_slider, (int)init_br, LV_ANIM_OFF);
    lv_obj_add_event_cb(_setup_slider, _slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_set_style_bg_color(_setup_slider, lv_color_hex(0x0288d1),
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(_setup_slider, lv_color_hex(0x4fc3f7),
                               LV_PART_KNOB     | LV_STATE_DEFAULT);

    // ── Status label ──────────────────────────────────────────────────────
    _lbl_status = lv_label_create(tab1);
    lv_label_set_text(_lbl_status, "Enter your WiFi credentials and ZIP code");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_width(_lbl_status, 720);
    lv_obj_set_pos(_lbl_status, 40, 384);
    lv_label_set_long_mode(_lbl_status, LV_LABEL_LONG_WRAP);

    // ════════════════════════════════════════════════════════════════════════
    // TAB 2 — Stocks & Teams
    // ════════════════════════════════════════════════════════════════════════

    // ── Section header: Stocks ─────────────────────────────────────────────
    lv_obj_t* stk_hdr = lv_label_create(tab2);
    lv_label_set_text(stk_hdr, "Stock Symbols");
    lv_obj_set_style_text_font(stk_hdr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(stk_hdr, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_pos(stk_hdr, 20, 10);

    // Column headers
    lv_obj_t* col_sym = lv_label_create(tab2);
    lv_label_set_text(col_sym, "Symbol");
    lv_obj_set_style_text_font(col_sym, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(col_sym, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(col_sym, 20, 38);

    lv_obj_t* col_nam = lv_label_create(tab2);
    lv_label_set_text(col_nam, "Display Name");
    lv_obj_set_style_text_font(col_nam, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(col_nam, lv_color_hex(0x888888), 0);
    lv_obj_set_pos(col_nam, 200, 38);

    // ── 6 stock rows ──────────────────────────────────────────────────────
    for (int i = 0; i < STOCK_COUNT; i++) {
        int y = 58 + i * 56;

        // Symbol textarea (max 10 chars — longest symbol is 5 + null)
        _ta_stock_sym[i] = lv_textarea_create(tab2);
        lv_obj_set_size(_ta_stock_sym[i], 160, 44);
        lv_obj_set_pos(_ta_stock_sym[i], 20, y);
        lv_textarea_set_one_line(_ta_stock_sym[i], true);
        lv_textarea_set_max_length(_ta_stock_sym[i], 10);
        lv_obj_set_style_text_font(_ta_stock_sym[i], &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(_ta_stock_sym[i], _stock_ta_cb, LV_EVENT_ALL, (void*)(intptr_t)i);

        // Display name textarea (max 20 chars)
        _ta_stock_nam[i] = lv_textarea_create(tab2);
        lv_obj_set_size(_ta_stock_nam[i], 260, 44);
        lv_obj_set_pos(_ta_stock_nam[i], 200, y);
        lv_textarea_set_one_line(_ta_stock_nam[i], true);
        lv_textarea_set_max_length(_ta_stock_nam[i], 20);
        lv_obj_set_style_text_font(_ta_stock_nam[i], &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(_ta_stock_nam[i], _stock_ta_cb, LV_EVENT_ALL, (void*)(intptr_t)i);

        // Pre-fill from prefs
        if (prefs && strlen(prefs->stock_symbols[i]) > 0) {
            lv_textarea_set_text(_ta_stock_sym[i], prefs->stock_symbols[i]);
            lv_textarea_set_text(_ta_stock_nam[i], prefs->stock_names[i]);
        }
    }

    // ── Section header: NBA Teams ──────────────────────────────────────────
    int teams_y = 58 + STOCK_COUNT * 56 + 16;

    lv_obj_t* team_hdr = lv_label_create(tab2);
    lv_label_set_text(team_hdr, "NBA Teams");
    lv_obj_set_style_text_font(team_hdr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(team_hdr, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_pos(team_hdr, 20, teams_y);

    // Build dropdown options string (newline-separated team names)
    static char _dd_opts[1024];
    _dd_opts[0] = '\0';
    for (int i = 0; i < _NBA_TEAM_COUNT; i++) {
        strncat(_dd_opts, _NBA_TEAMS[i].name, sizeof(_dd_opts) - strlen(_dd_opts) - 2);
        if (i < _NBA_TEAM_COUNT - 1) strncat(_dd_opts, "\n", sizeof(_dd_opts) - strlen(_dd_opts) - 1);
    }

    const char* dd_labels[2] = { "Team 1:", "Team 2:" };
    for (int d = 0; d < 2; d++) {
        int dy = teams_y + 28 + d * 64;

        lv_obj_t* lbl = lv_label_create(tab2);
        lv_label_set_text(lbl, dd_labels[d]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9e9e9e), 0);
        lv_obj_set_pos(lbl, 20, dy);

        _dd_nba[d] = lv_dropdown_create(tab2);
        lv_dropdown_set_options(_dd_nba[d], _dd_opts);
        lv_obj_set_size(_dd_nba[d], 420, 44);
        lv_obj_set_pos(_dd_nba[d], 100, dy);
        lv_obj_set_style_text_font(_dd_nba[d], &lv_font_montserrat_16, 0);
        lv_obj_set_style_bg_color(_dd_nba[d], lv_color_hex(0x1e2d4a), 0);
        lv_obj_set_style_border_color(_dd_nba[d], lv_color_hex(0x4fc3f7), 0);
        lv_obj_add_event_cb(_dd_nba[d], _team_dd_cb, LV_EVENT_VALUE_CHANGED, nullptr);

        // Pre-select saved team
        if (prefs) {
            int team_id = (d == 0) ? prefs->nba_team1_id : prefs->nba_team2_id;
            lv_dropdown_set_selected(_dd_nba[d], _nba_team_index(team_id));
        }
    }

    // ════════════════════════════════════════════════════════════════════════
    // Shared on-screen keyboard (child of scr so it overlays all tabs)
    // ════════════════════════════════════════════════════════════════════════
    _setup_keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(_setup_keyboard, SCREEN_WIDTH, 200);
    lv_obj_align(_setup_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(_setup_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);

    return scr;
}

// ─── Pre-fill fields on return to setup screen ────────────────────────────────
inline void ui_setup_refresh(AppPrefs* prefs) {
    if (!_setup_scr) return;
    if (prefs) {
        _setup_prefs_ptr = prefs;
        if (strlen(prefs->wifi_ssid) > 0) lv_textarea_set_text(_ta_ssid, prefs->wifi_ssid);
        if (strlen(prefs->zip_code)  > 0) lv_textarea_set_text(_ta_zip,  prefs->zip_code);
        if (_setup_slider) {
            lv_slider_set_value(_setup_slider, (int)prefs->night_brightness, LV_ANIM_OFF);
            char buf[32];
            snprintf(buf, sizeof(buf), "Night Brightness: %d%%", (int)prefs->night_brightness);
            if (_lbl_night_br) lv_label_set_text(_lbl_night_br, buf);
        }
        // Refresh stock fields
        for (int i = 0; i < STOCK_COUNT; i++) {
            if (_ta_stock_sym[i] && strlen(prefs->stock_symbols[i]) > 0)
                lv_textarea_set_text(_ta_stock_sym[i], prefs->stock_symbols[i]);
            if (_ta_stock_nam[i] && strlen(prefs->stock_names[i]) > 0)
                lv_textarea_set_text(_ta_stock_nam[i], prefs->stock_names[i]);
        }
        // Refresh team dropdowns
        if (_dd_nba[0]) lv_dropdown_set_selected(_dd_nba[0], _nba_team_index(prefs->nba_team1_id));
        if (_dd_nba[1]) lv_dropdown_set_selected(_dd_nba[1], _nba_team_index(prefs->nba_team2_id));
    }
    ui_setup_set_status("Enter your WiFi credentials and ZIP code.",
                        lv_color_hex(0xaaaaaa));
    lv_obj_add_flag(_setup_keyboard, LV_OBJ_FLAG_HIDDEN);
}
