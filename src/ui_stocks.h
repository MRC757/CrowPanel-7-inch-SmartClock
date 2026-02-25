#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_stocks.h — Full-screen market data view
//
// Layout (800 x 480):
//   Header bar (title + timestamp)         h=50
//   4 stock cards arranged in a 2x2 grid   h=400
//   Navigation bar                          h=30
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "stock_api.h"

static lv_obj_t* _stk_scr        = nullptr;
static lv_obj_t* _stk_hdr_time   = nullptr;
static time_t    _stk_last_fetch = 0;

// Card widgets (one per stock)
struct StockCard {
    lv_obj_t* card;
    lv_obj_t* lbl_name;
    lv_obj_t* lbl_symbol;
    lv_obj_t* lbl_price;
    lv_obj_t* lbl_change;
    lv_obj_t* lbl_chg_pct;
};
static StockCard _stk_cards[STOCK_COUNT];

// ─── Create one stock card ─────────────────────────────────────────────────
static StockCard make_stock_card(lv_obj_t* parent, int x, int y,
                                  int w, int h, int idx) {
    StockCard c;
    c.card = lv_obj_create(parent);
    lv_obj_set_size(c.card, w, h);
    lv_obj_set_pos(c.card, x, y);
    lv_obj_set_style_bg_color(c.card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(c.card, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(c.card, 1, 0);
    lv_obj_set_style_radius(c.card, 10, 0);
    lv_obj_set_style_pad_all(c.card, 14, 0);
    lv_obj_clear_flag(c.card, LV_OBJ_FLAG_SCROLLABLE);

    // Display name (e.g. "S&P 500")
    c.lbl_name = lv_label_create(c.card);
    lv_label_set_text(c.lbl_name, STOCK_DISPLAY_NAMES[idx]);
    lv_obj_set_style_text_font(c.lbl_name, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(c.lbl_name, lv_color_hex(0x4fc3f7), 0);
    lv_obj_set_pos(c.lbl_name, 0, 0);

    // Symbol (e.g. "^GSPC")
    c.lbl_symbol = lv_label_create(c.card);
    lv_label_set_text(c.lbl_symbol, STOCK_SYMBOLS[idx]);
    lv_obj_set_style_text_font(c.lbl_symbol, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(c.lbl_symbol, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_pos(c.lbl_symbol, 0, 26);

    // Price
    c.lbl_price = lv_label_create(c.card);
    lv_label_set_text(c.lbl_price, "---");
    lv_obj_set_style_text_font(c.lbl_price, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(c.lbl_price, lv_color_hex(0xffffff), 0);
    lv_obj_set_pos(c.lbl_price, 0, 52);

    // Absolute change
    c.lbl_change = lv_label_create(c.card);
    lv_label_set_text(c.lbl_change, "---");
    lv_obj_set_style_text_font(c.lbl_change, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(c.lbl_change, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_pos(c.lbl_change, 0, 98);

    // Percentage change (larger, coloured)
    c.lbl_chg_pct = lv_label_create(c.card);
    lv_label_set_text(c.lbl_chg_pct, "--.--%");
    lv_obj_set_style_text_font(c.lbl_chg_pct, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(c.lbl_chg_pct, lv_color_hex(0x9e9e9e), 0);
    lv_obj_set_pos(c.lbl_chg_pct, 0, 124);

    return c;
}

// ─── Create stocks screen ─────────────────────────────────────────────────
inline lv_obj_t* ui_stocks_create() {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _stk_scr = scr;

    // ── Header ────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 50);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, LV_SYMBOL_BARS "  Market Data");
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    _stk_hdr_time = lv_label_create(hdr);
    lv_label_set_text(_stk_hdr_time, "Not yet updated");
    lv_obj_set_style_text_font(_stk_hdr_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_stk_hdr_time, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(_stk_hdr_time, LV_ALIGN_RIGHT_MID, -14, 0);

    // ── 3×2 card grid (content area y=50, h=400) ─────────────────────────
    // 3 columns × 2 rows, 8px uniform gap on all sides and between cards.
    // card_w = (800 - 4*8) / 3 = 256,  card_h = (400 - 3*8) / 2 = 188
    int card_w = (SCREEN_WIDTH - 4 * 8) / 3;   // 256
    int card_h = (400          - 3 * 8) / 2;   // 188

    //          col 0     col 1     col 2
    // row 0:  STOCK[0]  STOCK[1]  STOCK[2]
    // row 1:  STOCK[3]  STOCK[4]  STOCK[5]
    int col_x[3] = { 8,  8 + card_w + 8,  8 + (card_w + 8) * 2 };   // 8, 272, 536
    int row_y[2] = { 50 + 8,  50 + 8 + card_h + 8 };                  // 58, 254
    int positions[STOCK_COUNT][2] = {
        { col_x[0], row_y[0] },   // S&P 500
        { col_x[1], row_y[0] },   // DOW JONES
        { col_x[2], row_y[0] },   // VYMI
        { col_x[0], row_y[1] },   // VYM
        { col_x[1], row_y[1] },   // GOLD
        { col_x[2], row_y[1] },   // SILVER
    };

    for (int i = 0; i < STOCK_COUNT; i++) {
        _stk_cards[i] = make_stock_card(scr,
                                         positions[i][0], positions[i][1],
                                         card_w, card_h, i);
    }

    // ── Navigation bar ────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_STOCKS);

    return scr;
}

// ─── Update all stock cards ───────────────────────────────────────────────
inline void ui_stocks_update(const StocksData& sd) {
    if (!_stk_scr) return;

    // Record fetch time and update header label when new valid data arrives
    if (sd.valid) {
        _stk_last_fetch = time(nullptr);
    }
    if (_stk_hdr_time && _stk_last_fetch > 0) {
        struct tm ti;
        localtime_r(&_stk_last_fetch, &ti);
        char ts[28];
        strftime(ts, sizeof(ts), "Updated %I:%M %p", &ti);
        // Strip leading zero from hour ("Updated 02:34 PM" → "Updated 2:34 PM")
        char* p = ts + 8;  // points to hour digit after "Updated "
        if (p[0] == '0') memmove(p, p + 1, strlen(p));
        lv_label_set_text(_stk_hdr_time, ts);
    }

    if (!sd.valid) return;

    for (int i = 0; i < STOCK_COUNT; i++) {
        const StockInfo& s = sd.stocks[i];
        StockCard&        c = _stk_cards[i];
        char buf[32];

        if (!s.valid) {
            lv_label_set_text(c.lbl_price,   "N/A");
            lv_label_set_text(c.lbl_change,  "--");
            lv_label_set_text(c.lbl_chg_pct, "--.--%");
            continue;
        }

        // Price (comma-formatted for large index values)
        if (s.price >= 10000.0f) {
            long pi = (long)s.price;
            snprintf(buf, sizeof(buf), "%ld,%03ld", pi / 1000, pi % 1000);
        } else if (s.price >= 1000.0f) {
            snprintf(buf, sizeof(buf), "%.0f", s.price);
        } else {
            snprintf(buf, sizeof(buf), "%.2f", s.price);
        }
        lv_label_set_text(c.lbl_price, buf);

        // Absolute change
        snprintf(buf, sizeof(buf), "%+.2f", s.change);
        lv_label_set_text(c.lbl_change, buf);

        // Percentage change
        snprintf(buf, sizeof(buf), "%+.2f%%", s.change_pct);
        lv_label_set_text(c.lbl_chg_pct, buf);

        // Colour based on direction
        bool up = (s.change_pct >= 0.0f);
        lv_color_t chg_color = up ? lv_color_hex(0x4caf50) : lv_color_hex(0xf44336);
        lv_obj_set_style_text_color(c.lbl_change,  chg_color, 0);
        lv_obj_set_style_text_color(c.lbl_chg_pct, chg_color, 0);
    }
}
