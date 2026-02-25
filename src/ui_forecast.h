#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_forecast.h — 5-day weather forecast screen
//
// Layout (800 x 480):
//   Header bar (title + timestamp)               h=50
//   5 vertical cards side-by-side                h=360
//     Each card: day name, TODAY badge (day 0),
//                condition, high °F, low °F, precip, UV index
//   ISS pass-time strip                          h=26
//   Navigation bar                               h=30
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "weather_api.h"
#include "iss_api.h"

static lv_obj_t* _fcast_scr      = nullptr;
static lv_obj_t* _fcast_hdr_time = nullptr;
static lv_obj_t* _fcast_iss_lbl  = nullptr;

struct ForecastCard {
    lv_obj_t* card;
    lv_obj_t* lbl_day;
    lv_obj_t* lbl_today;
    lv_obj_t* lbl_desc;
    lv_obj_t* lbl_high;
    lv_obj_t* lbl_low;
    lv_obj_t* lbl_precip;
    lv_obj_t* lbl_uv;
};
static ForecastCard _fcast_cards[FORECAST_DAYS];

// ─── Card geometry ───────────────────────────────────────────────────────────
// 5 cards, margins 7/6, card_w=151, gap=8  →  7 + 5×151 + 4×8 + 6 = 800 ✓
static constexpr int _FC_MARGIN_L = 7;
static constexpr int _FC_GAP      = 8;
static constexpr int _FC_W        = 151;
static constexpr int _FC_TOP      = 54;
static constexpr int _FC_H        = 360;

// ─── Build one forecast card ─────────────────────────────────────────────────
static ForecastCard _make_fcast_card(lv_obj_t* parent, int x, int idx) {
    ForecastCard c;

    c.card = lv_obj_create(parent);
    lv_obj_set_size(c.card, _FC_W, _FC_H);
    lv_obj_set_pos(c.card, x, _FC_TOP);
    lv_obj_set_style_bg_color(c.card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(c.card,
        (idx == 0) ? lv_color_hex(0x4fc3f7) : lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(c.card, (idx == 0) ? 2 : 1, 0);
    lv_obj_set_style_radius(c.card, 8, 0);
    lv_obj_set_style_pad_all(c.card, 10, 0);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);

    // Day abbreviation
    c.lbl_day = lv_label_create(c.card);
    lv_label_set_text(c.lbl_day, "---");
    lv_obj_set_style_text_font(c.lbl_day, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(c.lbl_day, lv_color_hex(0xffffff), 0);
    lv_obj_align(c.lbl_day, LV_ALIGN_TOP_MID, 0, 0);

    // "TODAY" badge — hidden for cards 1-4
    c.lbl_today = lv_label_create(c.card);
    lv_label_set_text(c.lbl_today, "TODAY");
    lv_obj_set_style_text_font(c.lbl_today, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(c.lbl_today, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(c.lbl_today, LV_ALIGN_TOP_MID, 0, 26);
    if (idx != 0) lv_obj_add_flag(c.lbl_today, LV_OBJ_FLAG_HIDDEN);

    // Thin divider
    lv_obj_t* div = lv_obj_create(c.card);
    lv_obj_set_size(div, _FC_W - 20, 1);
    lv_obj_set_pos(div, 0, 46);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Condition
    c.lbl_desc = lv_label_create(c.card);
    lv_label_set_text(c.lbl_desc, "---");
    lv_obj_set_style_text_font(c.lbl_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c.lbl_desc, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_width(c.lbl_desc, _FC_W - 20);
    lv_label_set_long_mode(c.lbl_desc, LV_LABEL_LONG_WRAP);
    lv_obj_align(c.lbl_desc, LV_ALIGN_TOP_MID, 0, 56);

    // "HI" tag
    lv_obj_t* hi_tag = lv_label_create(c.card);
    lv_label_set_text(hi_tag, "HI");
    lv_obj_set_style_text_font(hi_tag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hi_tag, lv_color_hex(0xff7043), 0);
    lv_obj_align(hi_tag, LV_ALIGN_TOP_MID, 0, 106);

    // High temperature
    c.lbl_high = lv_label_create(c.card);
    lv_label_set_text(c.lbl_high, "--°");
    lv_obj_set_style_text_font(c.lbl_high, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(c.lbl_high, lv_color_hex(0xffffff), 0);
    lv_obj_align(c.lbl_high, LV_ALIGN_TOP_MID, 0, 120);

    // "LO" tag
    lv_obj_t* lo_tag = lv_label_create(c.card);
    lv_label_set_text(lo_tag, "LO");
    lv_obj_set_style_text_font(lo_tag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lo_tag, lv_color_hex(0x64b5f6), 0);
    lv_obj_align(lo_tag, LV_ALIGN_TOP_MID, 0, 196);

    // Low temperature
    c.lbl_low = lv_label_create(c.card);
    lv_label_set_text(c.lbl_low, "--°");
    lv_obj_set_style_text_font(c.lbl_low, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(c.lbl_low, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(c.lbl_low, LV_ALIGN_TOP_MID, 0, 210);

    // Divider above precip
    lv_obj_t* div2 = lv_obj_create(c.card);
    lv_obj_set_size(div2, _FC_W - 20, 1);
    lv_obj_set_pos(div2, 0, 252);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div2, 0, 0);

    // "PRECIP" tag
    lv_obj_t* pr_tag = lv_label_create(c.card);
    lv_label_set_text(pr_tag, "PRECIP");
    lv_obj_set_style_text_font(pr_tag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pr_tag, lv_color_hex(0x78909c), 0);
    lv_obj_align(pr_tag, LV_ALIGN_TOP_MID, 0, 260);

    // Precipitation
    c.lbl_precip = lv_label_create(c.card);
    lv_label_set_text(c.lbl_precip, "-- in");
    lv_obj_set_style_text_font(c.lbl_precip, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c.lbl_precip, lv_color_hex(0x80cbc4), 0);
    lv_obj_align(c.lbl_precip, LV_ALIGN_TOP_MID, 0, 276);

    // Divider above UV
    lv_obj_t* div3 = lv_obj_create(c.card);
    lv_obj_set_size(div3, _FC_W - 20, 1);
    lv_obj_set_pos(div3, 0, 302);
    lv_obj_set_style_bg_color(div3, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(div3, 0, 0);

    // "UV IDX" tag
    lv_obj_t* uv_tag = lv_label_create(c.card);
    lv_label_set_text(uv_tag, "UV IDX");
    lv_obj_set_style_text_font(uv_tag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(uv_tag, lv_color_hex(0x78909c), 0);
    lv_obj_align(uv_tag, LV_ALIGN_TOP_MID, 0, 310);

    // UV index value
    c.lbl_uv = lv_label_create(c.card);
    lv_label_set_text(c.lbl_uv, "--");
    lv_obj_set_style_text_font(c.lbl_uv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c.lbl_uv, lv_color_hex(0xffe082), 0);
    lv_obj_align(c.lbl_uv, LV_ALIGN_TOP_MID, 0, 326);

    return c;
}

// ─── Create forecast screen ──────────────────────────────────────────────────
inline lv_obj_t* ui_forecast_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _fcast_scr = scr;

    // ── Header ────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 50);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, LV_SYMBOL_TINT "  5-Day Forecast");
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    _fcast_hdr_time = lv_label_create(hdr);
    lv_label_set_text(_fcast_hdr_time, "--:-- --");
    lv_obj_set_style_text_font(_fcast_hdr_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_fcast_hdr_time, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(_fcast_hdr_time, LV_ALIGN_RIGHT_MID, -14, 0);

    // ── 5 forecast cards ──────────────────────────────────────────────────
    for (int i = 0; i < FORECAST_DAYS; i++) {
        int x = _FC_MARGIN_L + i * (_FC_W + _FC_GAP);
        _fcast_cards[i] = _make_fcast_card(scr, x, i);
    }

    // ── ISS pass-time strip (y=416, h=26) ─────────────────────────────────
    lv_obj_t* iss_bg = lv_obj_create(scr);
    lv_obj_set_size(iss_bg, SCREEN_WIDTH, 26);
    lv_obj_set_pos(iss_bg, 0, 418);
    lv_obj_set_style_bg_color(iss_bg, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(iss_bg, 0, 0);
    lv_obj_set_style_radius(iss_bg, 0, 0);
    lv_obj_set_style_pad_left(iss_bg, 66, 0);
    lv_obj_set_style_pad_top(iss_bg, 5, 0);
    lv_obj_clear_flag(iss_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* iss_prefix = lv_label_create(iss_bg);
    lv_label_set_text(iss_prefix, LV_SYMBOL_GPS " ISS");
    lv_obj_set_style_text_font(iss_prefix, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(iss_prefix, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_pos(iss_prefix, -60, 0);

    _fcast_iss_lbl = lv_label_create(iss_bg);
    lv_label_set_text(_fcast_iss_lbl, "Waiting for data...");
    lv_obj_set_style_text_font(_fcast_iss_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_fcast_iss_lbl, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_width(_fcast_iss_lbl, SCREEN_WIDTH - 72);
    lv_label_set_long_mode(_fcast_iss_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(_fcast_iss_lbl, 30, 0);
    lv_obj_set_pos(_fcast_iss_lbl, 0, 0);

    // ── Navigation bar ────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_FORECAST);

    return scr;
}

// ─── Populate cards with fetched data ────────────────────────────────────────
inline void ui_forecast_update(const WeatherData& wd) {
    if (!_fcast_scr || wd.forecast_count == 0) return;

    for (int i = 0; i < FORECAST_DAYS; i++) {
        ForecastCard& c = _fcast_cards[i];
        if (i >= wd.forecast_count) {
            lv_label_set_text(c.lbl_day,    "N/A");
            lv_label_set_text(c.lbl_desc,   "");
            lv_label_set_text(c.lbl_high,   "--°");
            lv_label_set_text(c.lbl_low,    "--°");
            lv_label_set_text(c.lbl_precip, "N/A");
            lv_label_set_text(c.lbl_uv,     "--");
            continue;
        }

        const DailyForecast& f = wd.forecast[i];
        char buf[24];

        lv_label_set_text(c.lbl_day,  f.day_name);
        lv_label_set_text(c.lbl_desc, wmo_desc_short(f.weather_code));

        snprintf(buf, sizeof(buf), "%.0f\xB0""F", f.temp_max_f);
        lv_label_set_text(c.lbl_high, buf);

        snprintf(buf, sizeof(buf), "%.0f\xB0""F", f.temp_min_f);
        lv_label_set_text(c.lbl_low, buf);

        snprintf(buf, sizeof(buf), "%.2f in", f.precip_in);
        lv_label_set_text(c.lbl_precip, buf);

        snprintf(buf, sizeof(buf), "%.1f", f.uv_index_max);
        lv_label_set_text(c.lbl_uv, buf);
    }

    if (_fcast_hdr_time) {
        struct tm ti;
        if (getLocalTime(&ti, 100)) {
            char ts[20];
            strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
            char* p = ts + 8;
            if (p[0] == '0') memmove(p, p + 1, strlen(p));
            lv_label_set_text(_fcast_hdr_time, ts);
        }
    }
}

// ─── Update ISS pass-time strip ───────────────────────────────────────────────
inline void ui_forecast_update_iss(const IssData& id) {
    if (!_fcast_iss_lbl) return;

    if (!id.valid || id.count == 0) {
        lv_label_set_text(_fcast_iss_lbl,
            "ISS passes unavailable — register free at n2yo.com for pass times");
        return;
    }

    String text = "";
    for (int i = 0; i < id.count; i++) {
        if (i > 0) text += "   |   ";
        struct tm tm_info;
        localtime_r(&id.passes[i].risetime, &tm_info);
        char tbuf[20];
        strftime(tbuf, sizeof(tbuf), "%a %I:%M %p", &tm_info);
        if (tbuf[4] == '0') memmove(tbuf + 4, tbuf + 5, strlen(tbuf + 5) + 1);
        text += tbuf;
        text += " (";
        text += String(id.passes[i].duration / 60);
        text += "m)";
    }
    lv_label_set_text(_fcast_iss_lbl, text.c_str());
}

// ─── Refresh header timestamp ─────────────────────────────────────────────────
inline void ui_forecast_tick() {
    if (!_fcast_hdr_time) return;
    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        char ts[20];
        strftime(ts, sizeof(ts), "%I:%M %p", &ti);
        lv_label_set_text(_fcast_hdr_time, ts);
    }
}
