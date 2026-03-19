#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_nfl.h — NFL games screen (800 × 480)
//
// Layout:
//   ┌──────────────────────────────────────────────────────────────────────────┐
//   │  NFL — Next 7 Days                            Updated 2:15 PM          │ h=44
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │  Sun Jan 12  ← date header row (dark bg)                               │ h=28
//   │    BAL @ KC                                               3:00 PM       │ h=38
//   │    PHI @ WAS                                              1:00 PM       │ h=38
//   │  Mon Jan 13  ← next date header                                        │ h=28
//   │    LAR @ CHI                                              8:20 PM       │ h=38
//   │  (scrollable if > ~8 games)                                             │
//   ├──────────────────────────────────────────────────────────────────────────┤
//   │  [⚙ Setup] [🏠 Clock] [📰 News] [📈 Stocks] [🌤 Forecast] [▶ NFL]    │ h=30
//   └──────────────────────────────────────────────────────────────────────────┘
//
// Call ui_nfl_create() once; call ui_nfl_update(NflData&) after each fetch.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "nfl_api.h"

// ─── Widget handles ────────────────────────────────────────────────────────
static lv_obj_t*     _nfl_scr            = nullptr;
static lv_obj_t*     _nfl_list_panel     = nullptr;   // scrollable content area
static lv_obj_t*     _nfl_updated_lbl    = nullptr;   // "Updated HH:MM AM" in header
static unsigned long _nfl_last_update_ms = 0;         // millis() at last successful fetch

// ─── Row geometry ──────────────────────────────────────────────────────────
#define NFL_DATE_ROW_H  28    // date separator row
#define NFL_GAME_ROW_H  38    // individual game row

// ─── Colors (reuse main screen palette) ───────────────────────────────────
#define NFL_CLR_BG       0x1a1a2e
#define NFL_CLR_PANEL    0x16213e
#define NFL_CLR_HDR_BG   0x0d1117
#define NFL_CLR_ACCENT   0x4fc3f7
#define NFL_CLR_TEXT     0xe0e0e0
#define NFL_CLR_SUBTEXT  0x9e9e9e
#define NFL_CLR_LIVE     0x4caf50   // green for in-progress scores

// ─── Create the NFL screen ─────────────────────────────────────────────────
inline lv_obj_t* ui_nfl_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(NFL_CLR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _nfl_scr = scr;

    // ── Header bar (44 px) ─────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 44);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(NFL_CLR_HDR_BG), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "NFL Next 7 Days");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(NFL_CLR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    _nfl_updated_lbl = lv_label_create(hdr);
    lv_label_set_text(_nfl_updated_lbl, "");
    lv_obj_set_style_text_font(_nfl_updated_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_nfl_updated_lbl, lv_color_hex(NFL_CLR_SUBTEXT), 0);
    lv_obj_align(_nfl_updated_lbl, LV_ALIGN_RIGHT_MID, -12, 0);

    // ── Scrollable game list (y=44, height = screen – header – nav) ────────
    int list_h = SCREEN_HEIGHT - 44 - 30;
    _nfl_list_panel = lv_obj_create(scr);
    lv_obj_set_size(_nfl_list_panel, SCREEN_WIDTH, list_h);
    lv_obj_set_pos(_nfl_list_panel, 0, 44);
    lv_obj_set_style_bg_color(_nfl_list_panel, lv_color_hex(NFL_CLR_BG), 0);
    lv_obj_set_style_border_width(_nfl_list_panel, 0, 0);
    lv_obj_set_style_radius(_nfl_list_panel, 0, 0);
    lv_obj_set_style_pad_all(_nfl_list_panel, 0, 0);
    // Vertical scroll only; thin scrollbar on right
    lv_obj_set_scroll_dir(_nfl_list_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_nfl_list_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(_nfl_list_panel, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_nfl_list_panel, lv_color_hex(0x333355), LV_PART_SCROLLBAR);

    // Initial placeholder shown before first fetch
    lv_obj_t* ph = lv_label_create(_nfl_list_panel);
    lv_label_set_text(ph, "Loading NFL schedule...");
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ph, lv_color_hex(NFL_CLR_SUBTEXT), 0);
    lv_obj_set_pos(ph, 20, 20);

    // ── Navigation bar ─────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_NFL);

    return scr;
}

// ─── Rebuild the scrollable game list from fresh NflData ──────────────────
inline void ui_nfl_update(const NflData& nd) {
    if (!_nfl_list_panel) return;

    // Record fetch time and update header timestamp
    _nfl_last_update_ms = millis();
    if (_nfl_updated_lbl) {
        struct tm ti;
        if (getLocalTime(&ti, 100)) {
            char ts[20];
            strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
            lv_label_set_text(_nfl_updated_lbl, ts);
        }
        lv_obj_set_style_text_color(_nfl_updated_lbl, lv_color_hex(NFL_CLR_SUBTEXT), 0);
    }

    // Clear all existing children (removes placeholder too)
    lv_obj_clean(_nfl_list_panel);

    // ── No API key ─────────────────────────────────────────────────────────
    if (strlen(BALLDONTLIE_API_KEY) == 0) {
        lv_obj_t* lbl = lv_label_create(_nfl_list_panel);
        lv_label_set_text(lbl,
            "NFL scores require a free API key.\n\n"
            "1. Register at  balldontlie.io\n"
            "2. Copy your key\n"
            "3. Paste it into  include/secrets.h\n"
            "4. Re-flash the firmware");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(NFL_CLR_SUBTEXT), 0);
        lv_obj_set_width(lbl, SCREEN_WIDTH - 40);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(lbl, 20, 20);
        return;
    }

    // ── No games ───────────────────────────────────────────────────────────
    if (!nd.valid || nd.count == 0) {
        lv_obj_t* lbl = lv_label_create(_nfl_list_panel);
        lv_label_set_text(lbl, "No NFL games scheduled in the next 7 days.");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(NFL_CLR_SUBTEXT), 0);
        lv_obj_set_pos(lbl, 20, 20);
        return;
    }

    // ── Build rows ─────────────────────────────────────────────────────────
    int y = 0;
    char cur_date_key[16] = "";   // "Sun Jan 12" — track when day changes

    for (int i = 0; i < nd.count; i++) {
        const NflGame& g = nd.games[i];

        // ── Date separator row (when date changes) ─────────────────────────
        char date_key[16];
        snprintf(date_key, sizeof(date_key), "%s %s", g.day_str, g.date_str);

        if (strcmp(date_key, cur_date_key) != 0) {
            strncpy(cur_date_key, date_key, sizeof(cur_date_key) - 1);

            lv_obj_t* dr = lv_obj_create(_nfl_list_panel);
            lv_obj_set_size(dr, SCREEN_WIDTH, NFL_DATE_ROW_H);
            lv_obj_set_pos(dr, 0, y);
            lv_obj_set_style_bg_color(dr, lv_color_hex(NFL_CLR_HDR_BG), 0);
            lv_obj_set_style_border_width(dr, 0, 0);
            lv_obj_set_style_radius(dr, 0, 0);
            lv_obj_set_style_pad_all(dr, 0, 0);
            lv_obj_clear_flag(dr, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* dl = lv_label_create(dr);
            lv_label_set_text(dl, date_key);   // e.g., "Sun Jan 12"
            lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(dl, lv_color_hex(NFL_CLR_ACCENT), 0);
            lv_obj_align(dl, LV_ALIGN_LEFT_MID, 12, 0);

            y += NFL_DATE_ROW_H;
        }

        // ── Game row ──────────────────────────────────────────────────────
        lv_obj_t* gr = lv_obj_create(_nfl_list_panel);
        lv_obj_set_size(gr, SCREEN_WIDTH, NFL_GAME_ROW_H);
        lv_obj_set_pos(gr, 0, y);
        // Alternate row background for readability
        uint32_t row_bg = (i % 2 == 0) ? NFL_CLR_PANEL : NFL_CLR_BG;
        lv_obj_set_style_bg_color(gr, lv_color_hex(row_bg), 0);
        lv_obj_set_style_border_width(gr, 0, 0);
        lv_obj_set_style_radius(gr, 0, 0);
        lv_obj_set_style_pad_all(gr, 0, 0);
        lv_obj_clear_flag(gr, LV_OBJ_FLAG_SCROLLABLE);

        // Teams: "BAL @ KC"
        char teams[20];
        snprintf(teams, sizeof(teams), "%s @ %s", g.visitor_abbr, g.home_abbr);
        lv_obj_t* tl = lv_label_create(gr);
        lv_label_set_text(tl, teams);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(NFL_CLR_TEXT), 0);
        lv_obj_align(tl, LV_ALIGN_LEFT_MID, 28, 0);

        // Time / score (right-aligned)
        lv_obj_t* sl = lv_label_create(gr);
        lv_label_set_text(sl, g.time_str);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);

        // Color: grey for final, green for live, blue for upcoming
        uint32_t time_clr;
        if (g.is_final) {
            time_clr = NFL_CLR_SUBTEXT;
        } else if (g.has_score) {
            time_clr = NFL_CLR_LIVE;    // in-progress
        } else {
            time_clr = NFL_CLR_ACCENT;  // upcoming kickoff
        }
        lv_obj_set_style_text_color(sl, lv_color_hex(time_clr), 0);
        lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -16, 0);

        y += NFL_GAME_ROW_H;
    }

    // Ensure panel is tall enough for all rows (LVGL uses child positions
    // to compute the scrollable area; no explicit height set needed if children
    // extend beyond the panel's visible height)
    // Force a layout refresh
    lv_obj_scroll_to_y(_nfl_list_panel, 0, LV_ANIM_OFF);
}

// ─── Called when navigating to the NFL screen (refreshes staleness color) ──
inline void ui_nfl_tick() {
    if (!_nfl_updated_lbl || _nfl_last_update_ms == 0) return;
    // Color the header label based on data age vs normal update interval
    unsigned long age = millis() - _nfl_last_update_ms;
    lv_color_t c;
    if      (age > 4UL * NFL_UPDATE_MS) c = lv_color_hex(0xf44336);  // red  — very stale
    else if (age > 2UL * NFL_UPDATE_MS) c = lv_color_hex(0xff9800);  // orange — overdue
    else                                c = lv_color_hex(NFL_CLR_SUBTEXT);  // gray — fresh
    lv_obj_set_style_text_color(_nfl_updated_lbl, c, 0);
}
