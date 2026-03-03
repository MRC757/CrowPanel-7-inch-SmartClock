#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_nba.h — LA Lakers + Golden State Warriors games screen (800 × 480)
//
// Layout:
//   ┌──────────────────────────────────────────────────────────────────────────┐
//   │  NBA — Lakers & Warriors                      Updated 2:15 PM          │ h=44
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │  Mon Jan 20  ← date separator (dark bg)                                │ h=28
//   │    LAL @ GSW                                              7:30 PM       │ h=38
//   │    LAL @ PHX                                              9:00 PM       │ h=38
//   │  Wed Jan 22  ← next date                                               │ h=28
//   │    GSW @ DEN                                           110-98 Final     │ h=38
//   │  (scrollable if many games)                                             │
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │  nav bar                                                               │ h=30
//   └──────────────────────────────────────────────────────────────────────────┘
//
// Team color accents on game rows:
//   LAL (Lakers) — left 4px border in Lakers purple  #552583
//   GSW (Warriors) — left 4px border in Warriors blue #1D428A
//   LAL vs GSW — left 4px border split purple+blue (gradient not supported;
//                use gold #FDB927 as neutral accent)
//
// Call ui_nba_create() once; call ui_nba_update(NbaData&) after each fetch.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "nba_api.h"

// ─── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t*     _nba_scr            = nullptr;
static lv_obj_t*     _nba_list_panel     = nullptr;   // scrollable content area
static lv_obj_t*     _nba_updated_lbl    = nullptr;   // "Updated HH:MM AM" in header
static unsigned long _nba_last_update_ms = 0;         // millis() at last successful fetch

// ─── Row geometry ──────────────────────────────────────────────────────────
#define NBA_DATE_ROW_H  28    // date separator row height
#define NBA_GAME_ROW_H  38    // individual game row height

// ─── Colors ────────────────────────────────────────────────────────────────
#define NBA_CLR_BG        0x1a1a2e
#define NBA_CLR_PANEL     0x16213e
#define NBA_CLR_HDR_BG    0x0d1117
#define NBA_CLR_ACCENT    0xfdb927   // Lakers gold — header, date rows, tip-off time
#define NBA_CLR_TEXT      0xe0e0e0
#define NBA_CLR_SUBTEXT   0x9e9e9e
#define NBA_CLR_LAL       0x552583   // Lakers purple
#define NBA_CLR_GSW       0x1d428a   // Warriors royal blue
#define NBA_CLR_NEUTRAL   0xfdb927   // gold — used when LAL vs GSW matchup

// ─── Team accent strip color ───────────────────────────────────────────────
// Returns the 4px left-border color for a game row based on which tracked
// team(s) are playing.
static uint32_t _nba_row_accent(const char* vis, const char* hom) {
    bool has_lal = (strcmp(vis, "LAL") == 0 || strcmp(hom, "LAL") == 0);
    bool has_gsw = (strcmp(vis, "GSW") == 0 || strcmp(hom, "GSW") == 0);
    if (has_lal && has_gsw) return NBA_CLR_NEUTRAL;   // LAL @ GSW or vice-versa
    if (has_lal) return NBA_CLR_LAL;
    if (has_gsw) return NBA_CLR_GSW;
    return NBA_CLR_ACCENT;   // fallback (shouldn't occur with team filter)
}

// ─── Create the NBA screen ─────────────────────────────────────────────────
inline lv_obj_t* ui_nba_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(NBA_CLR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _nba_scr = scr;

    // ── Header bar (44 px) ─────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 44);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(NBA_CLR_HDR_BG), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_LOOP "  NBA Lakers & Warriors");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(NBA_CLR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    _nba_updated_lbl = lv_label_create(hdr);
    lv_label_set_text(_nba_updated_lbl, "");
    lv_obj_set_style_text_font(_nba_updated_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_nba_updated_lbl, lv_color_hex(NBA_CLR_SUBTEXT), 0);
    lv_obj_align(_nba_updated_lbl, LV_ALIGN_RIGHT_MID, -12, 0);

    // ── Scrollable game list (y=44, height = screen – header – nav) ────────
    int list_h = SCREEN_HEIGHT - 44 - 30;
    _nba_list_panel = lv_obj_create(scr);
    lv_obj_set_size(_nba_list_panel, SCREEN_WIDTH, list_h);
    lv_obj_set_pos(_nba_list_panel, 0, 44);
    lv_obj_set_style_bg_color(_nba_list_panel, lv_color_hex(NBA_CLR_BG), 0);
    lv_obj_set_style_border_width(_nba_list_panel, 0, 0);
    lv_obj_set_style_radius(_nba_list_panel, 0, 0);
    lv_obj_set_style_pad_all(_nba_list_panel, 0, 0);
    lv_obj_set_scroll_dir(_nba_list_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_nba_list_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(_nba_list_panel, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_nba_list_panel, lv_color_hex(0x333355), LV_PART_SCROLLBAR);

    // Initial placeholder shown before first fetch
    lv_obj_t* ph = lv_label_create(_nba_list_panel);
    lv_label_set_text(ph, "Loading NBA schedule...");
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ph, lv_color_hex(NBA_CLR_SUBTEXT), 0);
    lv_obj_set_pos(ph, 20, 20);

    // ── Navigation bar ─────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_NBA);

    return scr;
}

// ─── Rebuild the scrollable game list from fresh NbaData ──────────────────
inline void ui_nba_update(const NbaData& nd) {
    if (!_nba_list_panel) return;

    // Record fetch time and update header timestamp
    _nba_last_update_ms = millis();
    if (_nba_updated_lbl) {
        struct tm ti;
        if (getLocalTime(&ti, 100)) {
            char ts[24];
            strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
            lv_label_set_text(_nba_updated_lbl, ts);
        }
        lv_obj_set_style_text_color(_nba_updated_lbl, lv_color_hex(NBA_CLR_SUBTEXT), 0);
    }

    // Clear all existing children (removes placeholder too)
    lv_obj_clean(_nba_list_panel);

    // ── No API key ─────────────────────────────────────────────────────────
    if (strlen(BALLDONTLIE_API_KEY) == 0) {
        lv_obj_t* lbl = lv_label_create(_nba_list_panel);
        lv_label_set_text(lbl,
            "NBA scores require a free API key.\n\n"
            "1. Register at  balldontlie.io\n"
            "2. Copy your key\n"
            "3. Paste it into  include/secrets.h\n"
            "4. Re-flash the firmware");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(NBA_CLR_SUBTEXT), 0);
        lv_obj_set_width(lbl, SCREEN_WIDTH - 40);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(lbl, 20, 20);
        return;
    }

    // ── No games ───────────────────────────────────────────────────────────
    if (!nd.valid || nd.count == 0) {
        lv_obj_t* lbl = lv_label_create(_nba_list_panel);
        lv_label_set_text(lbl,
            "No Lakers or Warriors games scheduled in the next 7 days.");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(NBA_CLR_SUBTEXT), 0);
        lv_obj_set_width(lbl, SCREEN_WIDTH - 40);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(lbl, 20, 20);
        return;
    }

    // ── Build rows ─────────────────────────────────────────────────────────
    int y = 0;
    char cur_date_key[16] = "";   // "Mon Jan 20" — detect day changes

    for (int i = 0; i < nd.count; i++) {
        const NbaGame& g = nd.games[i];

        // ── Date separator row (when date changes) ─────────────────────────
        char date_key[16];
        snprintf(date_key, sizeof(date_key), "%s %s", g.day_str, g.date_str);

        if (strcmp(date_key, cur_date_key) != 0) {
            strncpy(cur_date_key, date_key, sizeof(cur_date_key) - 1);

            lv_obj_t* dr = lv_obj_create(_nba_list_panel);
            lv_obj_set_size(dr, SCREEN_WIDTH, NBA_DATE_ROW_H);
            lv_obj_set_pos(dr, 0, y);
            lv_obj_set_style_bg_color(dr, lv_color_hex(NBA_CLR_HDR_BG), 0);
            lv_obj_set_style_border_width(dr, 0, 0);
            lv_obj_set_style_radius(dr, 0, 0);
            lv_obj_set_style_pad_all(dr, 0, 0);
            lv_obj_clear_flag(dr, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* dl = lv_label_create(dr);
            lv_label_set_text(dl, date_key);   // e.g., "Mon Jan 20"
            lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(dl, lv_color_hex(NBA_CLR_ACCENT), 0);
            lv_obj_align(dl, LV_ALIGN_LEFT_MID, 12, 0);

            y += NBA_DATE_ROW_H;
        }

        // ── Game row ──────────────────────────────────────────────────────
        lv_obj_t* gr = lv_obj_create(_nba_list_panel);
        lv_obj_set_size(gr, SCREEN_WIDTH, NBA_GAME_ROW_H);
        lv_obj_set_pos(gr, 0, y);
        uint32_t row_bg = (i % 2 == 0) ? NBA_CLR_PANEL : NBA_CLR_BG;
        lv_obj_set_style_bg_color(gr, lv_color_hex(row_bg), 0);
        lv_obj_set_style_border_width(gr, 0, 0);
        lv_obj_set_style_radius(gr, 0, 0);
        lv_obj_set_style_pad_all(gr, 0, 0);
        lv_obj_clear_flag(gr, LV_OBJ_FLAG_SCROLLABLE);

        // 4px colored left-edge accent strip (Lakers purple / Warriors blue / gold)
        lv_obj_t* accent = lv_obj_create(gr);
        lv_obj_set_size(accent, 4, NBA_GAME_ROW_H);
        lv_obj_set_pos(accent, 0, 0);
        lv_obj_set_style_bg_color(accent,
            lv_color_hex(_nba_row_accent(g.visitor_abbr, g.home_abbr)), 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 0, 0);
        lv_obj_set_style_pad_all(accent, 0, 0);
        lv_obj_set_style_shadow_width(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

        // Teams: "LAL @ GSW"
        char teams[20];
        snprintf(teams, sizeof(teams), "%s @ %s", g.visitor_abbr, g.home_abbr);
        lv_obj_t* tl = lv_label_create(gr);
        lv_label_set_text(tl, teams);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(NBA_CLR_TEXT), 0);
        lv_obj_align(tl, LV_ALIGN_LEFT_MID, 14, 0);

        // Tip-off time (right-aligned, always gold)
        lv_obj_t* sl = lv_label_create(gr);
        lv_label_set_text(sl, g.time_str);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sl, lv_color_hex(NBA_CLR_ACCENT), 0);
        lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -16, 0);

        y += NBA_GAME_ROW_H;
    }

    lv_obj_scroll_to_y(_nba_list_panel, 0, LV_ANIM_OFF);
}

// ─── Called when navigating to the NBA screen (refreshes staleness color) ──
inline void ui_nba_tick() {
    if (!_nba_updated_lbl || _nba_last_update_ms == 0) return;
    unsigned long age = millis() - _nba_last_update_ms;
    lv_color_t c;
    if      (age > 4UL * NBA_UPDATE_MS) c = lv_color_hex(0xf44336);  // red  — very stale
    else if (age > 2UL * NBA_UPDATE_MS) c = lv_color_hex(0xff9800);  // orange — overdue
    else                                c = lv_color_hex(NBA_CLR_SUBTEXT);  // gray — fresh
    lv_obj_set_style_text_color(_nba_updated_lbl, c, 0);
}
