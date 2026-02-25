#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_news.h — Full-screen news headlines view
//
// Shows breaking news headlines from Webz.io News API Lite.
// Layout (800 x 480):
//   Header bar (title + timestamp)  h=50
//   Scrollable list of headlines     h=400
//   Navigation bar                   h=30
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "news_api.h"

static lv_obj_t* _news_scr      = nullptr;
static lv_obj_t* _news_list     = nullptr;
static lv_obj_t* _news_hdr_time = nullptr;

inline lv_obj_t* ui_news_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _news_scr = scr;

    // ── Header (800 x 50) ─────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 50);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, LV_SYMBOL_LIST "  Breaking News");
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    _news_hdr_time = lv_label_create(hdr);
    lv_label_set_text(_news_hdr_time, "--:-- --");
    lv_obj_set_style_text_font(_news_hdr_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_news_hdr_time, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(_news_hdr_time, LV_ALIGN_RIGHT_MID, -14, 0);

    // ── Scrollable headline list (y=50, h=400) ────────────────────────────
    _news_list = lv_list_create(scr);
    lv_obj_set_size(_news_list, SCREEN_WIDTH, 400);
    lv_obj_set_pos(_news_list, 0, 50);
    lv_obj_set_style_bg_color(_news_list, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(_news_list, 0, 0);
    lv_obj_set_style_radius(_news_list, 0, 0);
    lv_obj_set_style_pad_all(_news_list, 8, 0);
    lv_obj_set_style_pad_gap(_news_list, 4, 0);

    // Placeholder until data arrives
    lv_list_add_text(_news_list, "Fetching headlines...");

    // ── Navigation bar ────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_NEWS);

    return scr;
}

// ─── Update the list with fresh headlines ─────────────────────────────────
inline void ui_news_update(const NewsData& nd) {
    if (!_news_list) return;

    // Update timestamp in header
    if (_news_hdr_time) {
        struct tm ti;
        if (getLocalTime(&ti, 100)) {
            char ts[20];
            strftime(ts, sizeof(ts), "%I:%M %p", &ti);
            lv_label_set_text(_news_hdr_time, ts);
        }
    }

    if (!nd.valid || nd.count == 0) return;

    // Clear existing items and repopulate
    lv_obj_clean(_news_list);

    for (int i = 0; i < nd.count; i++) {
        // lv_list_add_btn creates a pressable item; we style it as plain text
        lv_obj_t* btn = lv_list_add_btn(_news_list, LV_SYMBOL_RIGHT, nd.headlines[i]);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xe0e0e0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn,
            (i % 2 == 0) ? lv_color_hex(0x16213e) : lv_color_hex(0x1a1a2e),
            LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_height(btn, LV_SIZE_CONTENT);
        // Allow multi-line wrapping
        lv_obj_t* lbl = lv_obj_get_child(btn, -1);
        if (lbl) {
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, SCREEN_WIDTH - 60);
        }
    }
}

// ─── Refresh timestamp without reloading headlines ────────────────────────
inline void ui_news_tick() {
    if (!_news_hdr_time) return;
    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        char ts[20];
        strftime(ts, sizeof(ts), "%I:%M %p", &ti);
        lv_label_set_text(_news_hdr_time, ts);
    }
}
