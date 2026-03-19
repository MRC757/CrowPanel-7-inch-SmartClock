#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_joke.h — Joke of the Moment screen (800 × 480)
//
// Layout:
//   ┌──────────────────────────────────────────────────────────────────────────┐
//   │  ⚡ Joke of the Moment                          Updated 2:15 PM        │ h=44
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │                                                                          │
//   │         Setup / question  (white, montserrat_24, centered)              │ h=203
//   │                                                                          │
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │                                                                          │
//   │         Punchline  (gold, montserrat_28, centered)                      │ h=203
//   │                                                                          │
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │  nav bar                                                                 │ h=30
//   └──────────────────────────────────────────────────────────────────────────┘
//
// For one-liners (no punchline) the setup panel expands to the full 406 px
// and the punchline panel is hidden.
//
// Call ui_joke_create() once in setup().
// Call ui_joke_update(setup, punchline) after a successful fetchJoke().
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"

#define JOKE_HEADER_H  44
#define JOKE_NAV_H     30
#define JOKE_CONTENT_H (SCREEN_HEIGHT - JOKE_HEADER_H - JOKE_NAV_H)  // 406 px

// ─── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t*     _joke_scr           = nullptr;
static lv_obj_t*     _joke_setup_panel   = nullptr;
static lv_obj_t*     _joke_punch_panel   = nullptr;
static lv_obj_t*     _joke_setup_lbl     = nullptr;
static lv_obj_t*     _joke_punchline_lbl = nullptr;
static lv_obj_t*     _joke_updated_lbl   = nullptr;
static unsigned long _joke_last_update_ms = 0;

// ─── Create the Joke screen ────────────────────────────────────────────────
inline lv_obj_t* ui_joke_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _joke_scr = scr;

    // ── Header bar (44 px) ─────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, JOKE_HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_CHARGE "  Joke of the Moment");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff9800), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    _joke_updated_lbl = lv_label_create(hdr);
    lv_label_set_text(_joke_updated_lbl, "");
    lv_obj_set_style_text_font(_joke_updated_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_joke_updated_lbl, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(_joke_updated_lbl, LV_ALIGN_RIGHT_MID, -12, 0);

    // ── Setup panel (top half of content, y = JOKE_HEADER_H) ──────────────
    _joke_setup_panel = lv_obj_create(scr);
    lv_obj_set_size(_joke_setup_panel, SCREEN_WIDTH, JOKE_CONTENT_H / 2);
    lv_obj_set_pos(_joke_setup_panel, 0, JOKE_HEADER_H);
    lv_obj_set_style_bg_color(_joke_setup_panel, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(_joke_setup_panel, 0, 0);
    lv_obj_set_style_radius(_joke_setup_panel, 0, 0);
    lv_obj_set_style_pad_hor(_joke_setup_panel, 24, 0);
    lv_obj_set_style_pad_ver(_joke_setup_panel, 12, 0);
    lv_obj_clear_flag(_joke_setup_panel, LV_OBJ_FLAG_SCROLLABLE);

    _joke_setup_lbl = lv_label_create(_joke_setup_panel);
    lv_label_set_text(_joke_setup_lbl, "");
    lv_obj_set_style_text_font(_joke_setup_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_joke_setup_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_label_set_long_mode(_joke_setup_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_joke_setup_lbl, SCREEN_WIDTH - 48);
    lv_obj_set_style_text_align(_joke_setup_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_joke_setup_lbl, LV_ALIGN_CENTER, 0, 0);

    // ── Punchline panel (bottom half, y = JOKE_HEADER_H + half) ───────────
    _joke_punch_panel = lv_obj_create(scr);
    lv_obj_set_size(_joke_punch_panel, SCREEN_WIDTH, JOKE_CONTENT_H / 2);
    lv_obj_set_pos(_joke_punch_panel, 0, JOKE_HEADER_H + JOKE_CONTENT_H / 2);
    lv_obj_set_style_bg_color(_joke_punch_panel, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(_joke_punch_panel, 0, 0);
    lv_obj_set_style_radius(_joke_punch_panel, 0, 0);
    lv_obj_set_style_pad_hor(_joke_punch_panel, 24, 0);
    lv_obj_set_style_pad_ver(_joke_punch_panel, 12, 0);
    lv_obj_clear_flag(_joke_punch_panel, LV_OBJ_FLAG_SCROLLABLE);

    _joke_punchline_lbl = lv_label_create(_joke_punch_panel);
    lv_label_set_text(_joke_punchline_lbl, "");
    lv_obj_set_style_text_font(_joke_punchline_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_joke_punchline_lbl, lv_color_hex(0xffd740), 0);
    lv_label_set_long_mode(_joke_punchline_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_joke_punchline_lbl, SCREEN_WIDTH - 48);
    lv_obj_set_style_text_align(_joke_punchline_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_joke_punchline_lbl, LV_ALIGN_CENTER, 0, 0);

    // ── Navigation bar ─────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_JOKE);

    return scr;
}

// ─── Update joke content ───────────────────────────────────────────────────
inline void ui_joke_update(const char* setup, const char* punchline) {
    _joke_last_update_ms = millis();

    bool has_punchline = (punchline && punchline[0] != '\0');

    if (has_punchline) {
        // Two-part joke: setup top half, punchline bottom half
        lv_obj_set_size(_joke_setup_panel, SCREEN_WIDTH, JOKE_CONTENT_H / 2);
        lv_obj_set_pos(_joke_setup_panel,  0, JOKE_HEADER_H);
        lv_obj_clear_flag(_joke_punch_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_joke_setup_lbl, &lv_font_montserrat_24, 0);
    } else {
        // One-liner: setup fills the full content area
        lv_obj_set_size(_joke_setup_panel, SCREEN_WIDTH, JOKE_CONTENT_H);
        lv_obj_set_pos(_joke_setup_panel,  0, JOKE_HEADER_H);
        lv_obj_add_flag(_joke_punch_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_joke_setup_lbl, &lv_font_montserrat_28, 0);
    }

    if (_joke_setup_lbl) {
        lv_label_set_text(_joke_setup_lbl, setup ? setup : "");
        lv_obj_align(_joke_setup_lbl, LV_ALIGN_CENTER, 0, 0);
    }
    if (_joke_punchline_lbl && has_punchline) {
        lv_label_set_text(_joke_punchline_lbl, punchline);
        lv_obj_align(_joke_punchline_lbl, LV_ALIGN_CENTER, 0, 0);
    }

    if (_joke_updated_lbl) {
        struct tm ti;
        if (getLocalTime(&ti, 100)) {
            char ts[24];
            strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
            lv_label_set_text(_joke_updated_lbl, ts);
        }
        lv_obj_set_style_text_color(_joke_updated_lbl, lv_color_hex(0x9e9e9e), 0);
    }
}

// ─── Called when navigating to the joke screen — refreshes staleness color ──
inline void ui_joke_tick() {
    if (!_joke_updated_lbl || _joke_last_update_ms == 0) return;
    unsigned long age = millis() - _joke_last_update_ms;
    lv_color_t c;
    if      (age > 4UL * JOKE_UPDATE_MS) c = lv_color_hex(0xf44336);  // red
    else if (age > 2UL * JOKE_UPDATE_MS) c = lv_color_hex(0xff9800);  // orange
    else                                  c = lv_color_hex(0x9e9e9e);  // gray
    lv_obj_set_style_text_color(_joke_updated_lbl, c, 0);
}
