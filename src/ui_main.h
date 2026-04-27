#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_main.h — Main clock screen (800 x 480)
//
// Layout:
//   ┌──────────────────────────────────────────────────────────────┐
//   │  [City, ST]        HH:MM:SS        Day, Mon DD YYYY  [⚙]   │ h=60
//   ├───────────────────────┬──────────────────────────────────────┤
//   │  WEATHER              │  MARKET                              │
//   │  72°F  Partly Cloudy  │  S&P 500   5,234.18  +0.45%  ▲     │
//   │  Feels like 70°F      │  DOW      39,123.45  -0.12%  ▼     │ h=360
//   │  Humidity 45%         │  VYMI         55.32  +0.23%  ▲     │
//   │  Wind 8.0 mph         │  VYM         123.45  +0.18%  ▲     │
//   ├───────────────────────┴──────────────────────────────────────┤
//   │  [⚙ Setup]   [🏠 Clock]   [📰 News]   [📈 Stocks]           │ h=30
//   └──────────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "weather_api.h"
#include "news_api.h"
#include "stock_api.h"
#include "moon.h"

// ─── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t* _main_scr        = nullptr;

// Header
static lv_obj_t* _lbl_city        = nullptr;
static lv_obj_t* _lbl_time        = nullptr;
static lv_obj_t* _lbl_date        = nullptr;
static lv_obj_t* _lbl_utc_time    = nullptr;

// Weather
static lv_obj_t* _lbl_temp        = nullptr;
static lv_obj_t* _lbl_wx_desc     = nullptr;
static lv_obj_t* _lbl_feels       = nullptr;
static lv_obj_t* _lbl_humidity    = nullptr;
static lv_obj_t* _lbl_wind        = nullptr;
static lv_obj_t* _lbl_sunrise     = nullptr;
static lv_obj_t* _lbl_sunset      = nullptr;
static lv_obj_t* _lbl_moon        = nullptr;
static lv_obj_t* _lbl_wx_updated  = nullptr;

// Stocks (STOCK_COUNT rows)
static lv_obj_t* _lbl_stock_name  [STOCK_COUNT];
static lv_obj_t* _lbl_stock_price [STOCK_COUNT];
static lv_obj_t* _lbl_stock_chg   [STOCK_COUNT];
static lv_obj_t* _lbl_stock_arrow [STOCK_COUNT];
static lv_obj_t* _lbl_stk_updated = nullptr;


// ─── Colors ────────────────────────────────────────────────────────────────
#define CLR_BG          0x1a1a2e
#define CLR_PANEL       0x16213e
#define CLR_ACCENT      0x4fc3f7
#define CLR_TEXT        0xe0e0e0
#define CLR_SUBTEXT     0x9e9e9e
#define CLR_UP          0x4caf50
#define CLR_DOWN        0xf44336
#define CLR_DIVIDER     0x333355

// ─── Helper: nav bar shared by all screens ────────────────────────────────
static void _create_nav_bar(lv_obj_t* scr, int active_screen) {
    const struct { const char* label; int id; } TABS[] = {
        { LV_SYMBOL_SETTINGS " Setup",   SCR_SETUP    },
        { LV_SYMBOL_HOME     " Clock",   SCR_MAIN     },
        { LV_SYMBOL_LIST     " News",    SCR_NEWS     },
        { LV_SYMBOL_BARS     " Stocks",  SCR_STOCKS   },
        { LV_SYMBOL_TINT     " Daily",   SCR_FORECAST },
        { LV_SYMBOL_REFRESH  " Hourly",  SCR_HOURLY   },
        { LV_SYMBOL_PLAY     " NFL",     SCR_NFL      },
        { LV_SYMBOL_LOOP     " NBA",     SCR_NBA      },
    };
    int n = (int)(sizeof(TABS) / sizeof(TABS[0]));
    int btn_w = SCREEN_WIDTH / n;

    for (int i = 0; i < n; i++) {
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, btn_w, 30);
        lv_obj_set_pos(btn, i * btn_w, SCREEN_HEIGHT - 30);
        lv_obj_set_style_radius(btn, 0, 0);

        bool isActive = (TABS[i].id == active_screen);
        lv_obj_set_style_bg_color(btn,
            isActive ? lv_color_hex(0x0288d1) : lv_color_hex(0x0d1117), 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        // Store screen ID in user_data
        lv_obj_set_user_data(btn, (void*)(intptr_t)TABS[i].id);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                navigateTo(id);
            }
        }, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, TABS[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
    }
}

// ─── Helper: vertical panel container ─────────────────────────────────────
static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(CLR_DIVIDER), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 6, 0);
    lv_obj_set_style_pad_all(p, 12, 0);
    lv_obj_set_style_shadow_width(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

// ─── Helper: styled label ─────────────────────────────────────────────────
static lv_obj_t* make_label(lv_obj_t* parent, const char* text,
                              const lv_font_t* font, lv_color_t color,
                              int x, int y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

// ─── Create main screen ────────────────────────────────────────────────────
inline lv_obj_t* ui_main_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);  // ensure bg fills all gaps between widgets
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _main_scr = scr;

    // ── Header bar (800 x 80) ─────────────────────────────────────────────
    // Height=80 matches the panel top (y=80) so there is no exposed gap
    // between the header bottom and the content panels.
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 80);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_shadow_width(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    _lbl_city = make_label(hdr, "---, --", &lv_font_montserrat_24,
                            lv_color_hex(0x80cbc4), 10, 28);

    _lbl_time = lv_label_create(hdr);
    lv_label_set_text(_lbl_time, "--:--:--");
    lv_obj_set_style_text_font(_lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_lbl_time, lv_color_hex(0xffffff), 0);
    lv_obj_align(_lbl_time, LV_ALIGN_TOP_MID, 0, 2);

    _lbl_date = make_label(hdr, "--- --- -- ----", &lv_font_montserrat_20,
                            lv_color_hex(0xb0bec5), 510, 4);

    _lbl_utc_time = make_label(hdr, "UTC --:--", &lv_font_montserrat_12,
                                lv_color_hex(0x78909c), 510, 28);

    // ── Two-column content area (800 x 370, y=80) ─────────────────────────
    // Weather panel (left, 390 wide)
    lv_obj_t* wx_panel = make_panel(scr, 4, 80, 390, 370);

    lv_obj_t* wx_hdr = lv_label_create(wx_panel);
    lv_label_set_text(wx_hdr, "WEATHER");
    lv_obj_set_style_text_font(wx_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wx_hdr, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_pos(wx_hdr, 0, 0);

    _lbl_temp = lv_label_create(wx_panel);
    lv_label_set_text(_lbl_temp, "--°F");
    lv_obj_set_style_text_font(_lbl_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(_lbl_temp, lv_color_hex(0xffffff), 0);
    lv_obj_align(_lbl_temp, LV_ALIGN_TOP_MID, 0, 24);

    _lbl_wx_desc = lv_label_create(wx_panel);
    lv_label_set_text(_lbl_wx_desc, "---");
    lv_obj_set_style_text_font(_lbl_wx_desc, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_wx_desc, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_align(_lbl_wx_desc, LV_ALIGN_TOP_MID, 0, 92);

    _lbl_feels = make_label(wx_panel, "Feels like: --°F",
                             &lv_font_montserrat_16, lv_color_hex(CLR_TEXT), 0, 132);
    _lbl_humidity = make_label(wx_panel, "Humidity: --%",
                                &lv_font_montserrat_16, lv_color_hex(CLR_TEXT), 0, 162);
    _lbl_wind = make_label(wx_panel, "Wind: -- mph",
                            &lv_font_montserrat_16, lv_color_hex(CLR_TEXT), 0, 192);

    _lbl_sunrise = make_label(wx_panel, "Rise: --:-- --",
                               &lv_font_montserrat_16, lv_color_hex(CLR_TEXT), 0, 224);
    _lbl_sunset  = make_label(wx_panel, "Set:  --:-- --",
                               &lv_font_montserrat_16, lv_color_hex(CLR_TEXT), 0, 252);
    _lbl_moon    = make_label(wx_panel, "Moon: ---",
                               &lv_font_montserrat_16, lv_color_hex(0x90caf9), 0, 280);

    _lbl_wx_updated = make_label(wx_panel, "Not yet updated",
                                  &lv_font_montserrat_12, lv_color_hex(CLR_SUBTEXT),
                                  0, 310);

    // Stocks panel (right, 398 wide)
    lv_obj_t* stk_panel = make_panel(scr, 402, 80, 394, 370);

    lv_obj_t* stk_hdr = lv_label_create(stk_panel);
    lv_label_set_text(stk_hdr, "MARKET");
    lv_obj_set_style_text_font(stk_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stk_hdr, lv_color_hex(CLR_ACCENT), 0);
    lv_obj_set_pos(stk_hdr, 0, 0);

    // Stock rows — row_h derived so all STOCK_COUNT rows + the "Updated" label
    // fit inside the panel content area (386px - 24px padding = 362px).
    // With 6 stocks: 24 (header) + 6*50 (rows) + 14 (updated label) = 338 ≤ 362.
    int row_h = 50;
    for (int i = 0; i < STOCK_COUNT; i++) {
        int ry = 24 + i * row_h;

        _lbl_stock_name[i] = make_label(stk_panel, STOCK_NAMES_DEFAULT[i],
                                         &lv_font_montserrat_14,
                                         lv_color_hex(CLR_SUBTEXT), 0, ry);

        _lbl_stock_price[i] = make_label(stk_panel, "--",
                                          &lv_font_montserrat_24,
                                          lv_color_hex(CLR_TEXT), 0, ry + 18);

        _lbl_stock_chg[i] = make_label(stk_panel, "--",
                                        &lv_font_montserrat_14,
                                        lv_color_hex(CLR_SUBTEXT), 180, ry + 26);

        _lbl_stock_arrow[i] = make_label(stk_panel, "-",
                                          &lv_font_montserrat_20,
                                          lv_color_hex(CLR_SUBTEXT), 340, ry + 22);

        // Thin divider under each row (except last)
        if (i < STOCK_COUNT - 1) {
            lv_obj_t* div = lv_obj_create(stk_panel);
            lv_obj_set_size(div, 360, 1);
            lv_obj_set_pos(div, 0, ry + row_h - 4);
            lv_obj_set_style_bg_color(div, lv_color_hex(CLR_DIVIDER), 0);
            lv_obj_set_style_border_width(div, 0, 0);
        }
    }

    _lbl_stk_updated = make_label(stk_panel, "Not yet updated",
                                   &lv_font_montserrat_12, lv_color_hex(CLR_SUBTEXT),
                                   0, 24 + STOCK_COUNT * row_h);

    // ── Navigation bar (y=450) ─────────────────────────────────────────────
    _create_nav_bar(scr, SCR_MAIN);

    return scr;
}

// ─── Update clock display (called every second from an LVGL timer) ─────────
inline void ui_main_update_clock() {
    if (!_lbl_time) return;
    struct tm ti;
    if (!getLocalTime(&ti, 100)) return;

    // Change-detection statics: avoid invalidating labels that haven't changed.
    // _lbl_date and _lbl_utc_time live in the header; _lbl_moon is at y≈344 in
    // the weather panel.  Calling lv_label_set_text() unconditionally every second
    // expands the LVGL dirty region from ~40 px tall (time label only) to ~350 px
    // tall (spanning from the header down to the moon label), roughly 8× more
    // scanlines to flush → 8× larger cacheWriteBack burst → visible horizontal shift.
    static int  prev_mday    = -1;
    static int  prev_mon     = -1;
    static int  prev_utc_min = -1;
    static char prev_moon[24] = "";

    // Time — always changes every second
    char time_buf[12];
    strftime(time_buf, sizeof(time_buf), "%I:%M:%S", &ti);
    const char* t = (time_buf[0] == '0') ? time_buf + 1 : time_buf;
    lv_label_set_text(_lbl_time, t);

    // Date — only changes at midnight
    if (ti.tm_mday != prev_mday || ti.tm_mon != prev_mon) {
        char date_buf[32];
        strftime(date_buf, sizeof(date_buf), "%a %b %d %Y", &ti);
        lv_label_set_text(_lbl_date, date_buf);
        prev_mday = ti.tm_mday;
        prev_mon  = ti.tm_mon;
    }

    // UTC time — only changes each minute
    if (_lbl_utc_time && ti.tm_min != prev_utc_min) {
        time_t now = time(nullptr);
        struct tm* utc = gmtime(&now);
        char utc_buf[12];
        strftime(utc_buf, sizeof(utc_buf), "UTC %H:%M", utc);
        lv_label_set_text(_lbl_utc_time, utc_buf);
        prev_utc_min = ti.tm_min;
    }

    // Moon phase — changes every few days; compare string before updating
    if (_lbl_moon) {
        time_t now = time(nullptr);
        char moon_buf[24];
        snprintf(moon_buf, sizeof(moon_buf), "Moon: %s", moon_phase_name(now));
        if (strcmp(moon_buf, prev_moon) != 0) {
            lv_label_set_text(_lbl_moon, moon_buf);
            strncpy(prev_moon, moon_buf, sizeof(prev_moon) - 1);
        }
    }
}

// ─── Update weather panel ──────────────────────────────────────────────────
inline void ui_main_update_weather(const WeatherData& wd) {
    if (!_lbl_temp || !wd.valid) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f°F", wd.temp_f);
    lv_label_set_text(_lbl_temp, buf);

    lv_label_set_text(_lbl_wx_desc, wd.description);
    lv_label_set_text(_lbl_city, wd.city_name);

    snprintf(buf, sizeof(buf), "Feels like: %.0f°F", wd.feels_like_f);
    lv_label_set_text(_lbl_feels, buf);

    snprintf(buf, sizeof(buf), "Humidity: %d%%", wd.humidity_pct);
    lv_label_set_text(_lbl_humidity, buf);

    snprintf(buf, sizeof(buf), "Wind: %.1f mph", wd.wind_mph);
    lv_label_set_text(_lbl_wind, buf);

    // Sunrise / sunset from today's daily forecast (index 0)
    if (_lbl_sunrise && wd.forecast_count > 0) {
        snprintf(buf, sizeof(buf), "Rise: %s", wd.forecast[0].sunrise_str);
        lv_label_set_text(_lbl_sunrise, buf);
    }
    if (_lbl_sunset && wd.forecast_count > 0) {
        snprintf(buf, sizeof(buf), "Set:  %s", wd.forecast[0].sunset_str);
        lv_label_set_text(_lbl_sunset, buf);
    }

    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        char ts[20];
        strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
        lv_label_set_text(_lbl_wx_updated, ts);
    }
}

// ─── Update stocks panel ───────────────────────────────────────────────────
inline void ui_main_update_stocks(const StocksData& sd) {
    if (!sd.valid) return;

    for (int i = 0; i < STOCK_COUNT; i++) {
        const StockInfo& s = sd.stocks[i];
        char buf[32];

        if (!s.valid) {
            lv_label_set_text(_lbl_stock_price[i], "N/A");
            lv_label_set_text(_lbl_stock_chg[i], "");
            lv_label_set_text(_lbl_stock_arrow[i], "-");
            lv_obj_set_style_text_color(_lbl_stock_arrow[i], lv_color_hex(CLR_SUBTEXT), 0);
            continue;
        }

        // Price: indices can be 4- or 5-digit; ETFs are 2–3-digit
        // Note: %,' (comma grouping) is not supported on ESP32 newlib, so we
        // format manually: print as integer then insert commas every 3 digits.
        if (s.price >= 1000.0f) {
            // Format large index values without decimal (e.g. "5,234")
            long pi = (long)s.price;
            if (pi >= 10000)
                snprintf(buf, sizeof(buf), "%ld,%03ld", pi / 1000, pi % 1000);
            else
                snprintf(buf, sizeof(buf), "%ld", pi);
        } else {
            snprintf(buf, sizeof(buf), "%.2f", s.price);
        }
        lv_label_set_text(_lbl_stock_price[i], buf);

        // Change %
        snprintf(buf, sizeof(buf), "%+.2f%%", s.change_pct);
        lv_label_set_text(_lbl_stock_chg[i], buf);

        // Arrow and colour
        bool up = (s.change_pct >= 0.0f);
        lv_label_set_text(_lbl_stock_arrow[i], up ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
        lv_color_t chg_color = up ? lv_color_hex(CLR_UP) : lv_color_hex(CLR_DOWN);
        lv_obj_set_style_text_color(_lbl_stock_chg[i],   chg_color, 0);
        lv_obj_set_style_text_color(_lbl_stock_arrow[i], chg_color, 0);
        lv_obj_set_style_text_color(_lbl_stock_price[i], lv_color_hex(CLR_TEXT), 0);
    }

    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        char ts[20];
        strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
        lv_label_set_text(_lbl_stk_updated, ts);
    }
}

// ─── Update news ticker (removed — news available on the News screen) ───────
inline void ui_main_update_news(const NewsData&) {}
