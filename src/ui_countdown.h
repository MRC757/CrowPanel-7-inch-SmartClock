#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_countdown.h — Up to COUNTDOWN_COUNT user-defined countdowns (800 x 480)
//
// Each row is a lightweight tappable label trio (title + target Month/Day +
// "N days" readout). Tapping a row opens ONE shared edit popup (title
// textarea + on-screen keyboard + Month/Day rollers) — created only while
// open, destroyed
// (lv_obj_del) on close, same lazy pattern as the WiFi scan popup in
// ui_setup.h. This keeps only 2 rollers alive at any moment instead of
// COUNTDOWN_COUNT*2 permanently — rollers are heavy LVGL widgets, and this
// screen's malloc-backed heap competes directly with mbedTLS SSL handshakes
// for the same internal SRAM (LV_MEM_CUSTOM=1, see lv_conf.h). An earlier
// version kept all rollers/textareas/keyboard permanently allocated and
// measurably starved SSL fetches right after boot.
//
// No year is stored or picked: every countdown always counts down to the
// NEXT occurrence of its month/day, rolling forward to next year once that
// date has passed this year. There is no "N days ago" state.
//
// All state is read from / written directly to AppPrefs and persisted via
// prefs_save_countdown() — there is no separate fetch step.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "prefs_mgr.h"

#define CD_ROW_H       90
#define CD_CLR_BG      0x1a1a2e
#define CD_CLR_PANEL   0x16213e
#define CD_CLR_HDR_BG  0x0d1117
#define CD_CLR_ACCENT  0x4fc3f7
#define CD_CLR_TEXT    0xe0e0e0
#define CD_CLR_SUBTEXT 0x9e9e9e
#define CD_CLR_TODAY   0x4caf50

static lv_obj_t* _cd_scr          = nullptr;
static lv_obj_t* _cd_list_panel   = nullptr;
static lv_obj_t* _cd_lbl_title[COUNTDOWN_COUNT] = {};
static lv_obj_t* _cd_lbl_date [COUNTDOWN_COUNT] = {};
static lv_obj_t* _cd_lbl_days [COUNTDOWN_COUNT] = {};
static AppPrefs* _cd_prefs_ptr    = nullptr;

static const char* _CD_MONTH_ABBR[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

// ── Lazily-created editor popup (nullptr when closed) ──────────────────────
static lv_obj_t* _cd_popup      = nullptr;
static lv_obj_t* _cd_popup_ta   = nullptr;
static lv_obj_t* _cd_popup_mon  = nullptr;
static lv_obj_t* _cd_popup_day  = nullptr;
static lv_obj_t* _cd_popup_kb   = nullptr;
static int       _cd_editing_idx = -1;

static const char* _CD_MONTH_OPTS =
    "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";

static int _cd_days_in_month(int month1to12, int year) {
    static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month1to12 < 1 || month1to12 > 12) return 31;
    int d = dim[month1to12 - 1];
    if (month1to12 == 2) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (leap) d = 29;
    }
    return d;
}

// ─── Recompute and display the "N days" readout for one row ────────────────
// Always counts forward: builds this year's date first, and if that has
// already passed (or is today, which shows TODAY! instead), rolls to next
// year. Never shows a negative/"ago" count.
static void _cd_render_days(int idx) {
    if (!_cd_lbl_title[idx] || !_cd_lbl_date[idx] || !_cd_lbl_days[idx] || !_cd_prefs_ptr) return;

    const char* title = _cd_prefs_ptr->countdown_title[idx];
    bool configured = (strlen(title) > 0 && _cd_prefs_ptr->countdown_month[idx] != 0);

    lv_label_set_text(_cd_lbl_title[idx],
        configured ? title : "Tap to set up a countdown...");
    lv_obj_set_style_text_color(_cd_lbl_title[idx],
        lv_color_hex(configured ? CD_CLR_TEXT : CD_CLR_SUBTEXT), 0);

    if (!configured) {
        lv_label_set_text(_cd_lbl_date[idx], "");
        lv_label_set_text(_cd_lbl_days[idx], "");
        return;
    }

    {
        char datebuf[16];
        snprintf(datebuf, sizeof(datebuf), "%s %d",
                 _CD_MONTH_ABBR[_cd_prefs_ptr->countdown_month[idx] - 1],
                 _cd_prefs_ptr->countdown_day[idx]);
        lv_label_set_text(_cd_lbl_date[idx], datebuf);
    }

    struct tm now_tm;
    if (!getLocalTime(&now_tm, 100)) {
        lv_label_set_text(_cd_lbl_days[idx], "");
        return;
    }
    struct tm today = now_tm;
    today.tm_hour = 12; today.tm_min = 0; today.tm_sec = 0; today.tm_isdst = -1;
    time_t today_epoch = mktime(&today);
    int this_year = now_tm.tm_year + 1900;

    int month = _cd_prefs_ptr->countdown_month[idx];
    int day   = _cd_prefs_ptr->countdown_day[idx];
    int max_day = _cd_days_in_month(month, this_year);
    if (day > max_day) day = max_day;   // e.g. Feb 29 target evaluated in a non-leap year

    struct tm target = {};
    target.tm_year = this_year - 1900;
    target.tm_mon  = month - 1;
    target.tm_mday = day;
    target.tm_hour = 12; target.tm_isdst = -1;
    time_t target_epoch = mktime(&target);

    // Already passed this year (and not today) — roll to next year.
    if (target_epoch < today_epoch) {
        int next_year = this_year + 1;
        int max_day_next = _cd_days_in_month(month, next_year);
        target.tm_year = next_year - 1900;
        target.tm_mday = (_cd_prefs_ptr->countdown_day[idx] > max_day_next)
                          ? max_day_next : _cd_prefs_ptr->countdown_day[idx];
        target_epoch = mktime(&target);
    }

    long diff_sec = (long)(target_epoch - today_epoch);
    int days = (int)((diff_sec + 43200) / 86400);   // both anchored at noon; always >= 0

    char buf[24];
    lv_color_t color;
    if (days <= 0) {
        strncpy(buf, "TODAY!", sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
        color = lv_color_hex(CD_CLR_TODAY);
    } else {
        snprintf(buf, sizeof(buf), "%d DAY%s", days, (days == 1) ? "" : "S");
        color = lv_color_hex(CD_CLR_ACCENT);
    }
    lv_label_set_text(_cd_lbl_days[idx], buf);
    lv_obj_set_style_text_color(_cd_lbl_days[idx], color, 0);
}

// ─── Popup: rebuild the Day roller for the currently selected month ────────
static void _cd_popup_rebuild_day_roller() {
    if (!_cd_popup_mon || !_cd_popup_day) return;
    int month = lv_roller_get_selected(_cd_popup_mon) + 1;
    struct tm now_tm = {};
    getLocalTime(&now_tm, 100);
    int max_day = _cd_days_in_month(month, now_tm.tm_year + 1900);
    int cur_day = lv_roller_get_selected(_cd_popup_day) + 1;
    if (cur_day > max_day) cur_day = max_day;

    char opts[128];
    int n = 0;
    for (int d = 1; d <= max_day; d++)
        n += snprintf(opts + n, sizeof(opts) - n, "%s%d", (d > 1) ? "\n" : "", d);
    lv_roller_set_options(_cd_popup_day, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(_cd_popup_day, cur_day - 1, LV_ANIM_OFF);
}

static void _cd_popup_roller_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    _cd_popup_rebuild_day_roller();
}

// ─── Close the popup, freeing every widget it owns ──────────────────────────
static void _cd_close_popup() {
    if (_cd_popup) { lv_obj_del(_cd_popup); _cd_popup = nullptr; }
    _cd_popup_ta = _cd_popup_mon = _cd_popup_day = _cd_popup_kb = nullptr;
    _cd_editing_idx = -1;
}

static void _cd_save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = _cd_editing_idx;
    if (!_cd_prefs_ptr || idx < 0 || idx >= COUNTDOWN_COUNT) { _cd_close_popup(); return; }

    const char* title = lv_textarea_get_text(_cd_popup_ta);
    int month = lv_roller_get_selected(_cd_popup_mon) + 1;
    int day   = lv_roller_get_selected(_cd_popup_day) + 1;

    strncpy(_cd_prefs_ptr->countdown_title[idx], title,
            sizeof(_cd_prefs_ptr->countdown_title[idx]) - 1);
    _cd_prefs_ptr->countdown_title[idx][sizeof(_cd_prefs_ptr->countdown_title[idx]) - 1] = '\0';
    _cd_prefs_ptr->countdown_month[idx] = (int8_t)month;
    _cd_prefs_ptr->countdown_day[idx]   = (int8_t)day;
    prefs_save_countdown(idx, title, month, day);

    _cd_close_popup();
    _cd_render_days(idx);
}

static void _cd_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _cd_close_popup();
}

static void _cd_popup_ta_focus_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (!_cd_popup_kb) return;
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(_cd_popup_kb, _cd_popup_ta);
        lv_obj_clear_flag(_cd_popup_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        lv_obj_add_flag(_cd_popup_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─── Row tap: build the editor popup for that row ───────────────────────────
static void _cd_row_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!_cd_prefs_ptr || idx < 0 || idx >= COUNTDOWN_COUNT || !_cd_scr) return;

    _cd_close_popup();   // safety: should already be closed, never two at once
    _cd_editing_idx = idx;

    struct tm now_tm = {};
    getLocalTime(&now_tm, 100);

    bool has_date = (_cd_prefs_ptr->countdown_month[idx] != 0);
    int def_month = has_date ? _cd_prefs_ptr->countdown_month[idx] : now_tm.tm_mon + 1;
    int def_day   = has_date ? _cd_prefs_ptr->countdown_day[idx]   : now_tm.tm_mday;
    if (def_month < 1 || def_month > 12) def_month = 1;

    lv_obj_t* p = lv_obj_create(_cd_scr);
    _cd_popup = p;
    lv_obj_set_size(p, 500, 340);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x1e2d4a), 0);
    lv_obj_set_style_border_color(p, lv_color_hex(CD_CLR_ACCENT), 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_pad_all(p, 12, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ptitle = lv_label_create(p);
    char hdrbuf[24];
    snprintf(hdrbuf, sizeof(hdrbuf), "Edit Countdown #%d", idx + 1);
    lv_label_set_text(ptitle, hdrbuf);
    lv_obj_set_style_text_font(ptitle, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ptitle, lv_color_hex(CD_CLR_ACCENT), 0);
    lv_obj_set_pos(ptitle, 0, 0);

    lv_obj_t* ta = lv_textarea_create(p);
    _cd_popup_ta = ta;
    lv_obj_set_size(ta, 460, 44);
    lv_obj_set_pos(ta, 0, 34);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, COUNTDOWN_TITLE_LEN - 1);
    lv_textarea_set_placeholder_text(ta, "Countdown title...");
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_18, 0);
    if (has_date) lv_textarea_set_text(ta, _cd_prefs_ptr->countdown_title[idx]);
    lv_obj_add_event_cb(ta, _cd_popup_ta_focus_cb, LV_EVENT_ALL, nullptr);

    lv_obj_t* mon = lv_roller_create(p);
    _cd_popup_mon = mon;
    lv_roller_set_options(mon, _CD_MONTH_OPTS, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(mon, 160, 90);
    lv_obj_set_pos(mon, 60, 96);
    lv_obj_set_style_text_font(mon, &lv_font_montserrat_16, 0);
    lv_roller_set_selected(mon, def_month - 1, LV_ANIM_OFF);
    lv_obj_add_event_cb(mon, _cd_popup_roller_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* day = lv_roller_create(p);
    _cd_popup_day = day;
    lv_obj_set_size(day, 110, 90);
    lv_obj_set_pos(day, 240, 96);
    lv_obj_set_style_text_font(day, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(day, _cd_popup_roller_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Day roller options depend on month — populate now that it exists.
    int max_day = _cd_days_in_month(def_month, now_tm.tm_year + 1900);
    if (def_day > max_day) def_day = max_day;
    char day_opts[128];
    int n = 0;
    for (int d = 1; d <= max_day; d++)
        n += snprintf(day_opts + n, sizeof(day_opts) - n, "%s%d", (d > 1) ? "\n" : "", d);
    lv_roller_set_options(day, day_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(day, def_day - 1, LV_ANIM_OFF);

    lv_obj_t* save_btn = lv_btn_create(p);
    lv_obj_set_size(save_btn, 160, 44);
    lv_obj_set_pos(save_btn, 80, 220);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x0288d1), 0);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_add_event_cb(save_btn, _cd_save_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK "  Save");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(save_lbl);

    lv_obj_t* cancel_btn = lv_btn_create(p);
    lv_obj_set_size(cancel_btn, 160, 44);
    lv_obj_set_pos(cancel_btn, 260, 220);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_add_event_cb(cancel_btn, _cd_cancel_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE "  Cancel");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(cancel_lbl);

    // Keyboard — child of the popup, so it is destroyed along with it.
    lv_obj_t* kb = lv_keyboard_create(p);
    _cd_popup_kb = kb;
    lv_obj_set_size(kb, 500, 130);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 12);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

// ─── Build one lightweight countdown row (label pair only) ─────────────────
static void _cd_build_row(lv_obj_t* parent, int idx, int y) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, SCREEN_WIDTH, CD_ROW_H);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_style_bg_color(row, (idx % 2 == 0) ? lv_color_hex(CD_CLR_PANEL) : lv_color_hex(CD_CLR_BG), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, _cd_row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t* tl = lv_label_create(row);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_18, 0);
    lv_obj_set_width(tl, 340);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(tl, 14, 10);
    _cd_lbl_title[idx] = tl;

    lv_obj_t* dtl = lv_label_create(row);
    lv_obj_set_style_text_font(dtl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dtl, lv_color_hex(CD_CLR_SUBTEXT), 0);
    lv_label_set_text(dtl, "");
    lv_obj_set_pos(dtl, 14, 44);
    _cd_lbl_date[idx] = dtl;

    lv_obj_t* dl = lv_label_create(row);
    lv_obj_set_style_text_font(dl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(dl, lv_color_hex(CD_CLR_ACCENT), 0);
    lv_label_set_text(dl, "");
    lv_obj_set_width(dl, 380);
    lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(dl, 406, 28);
    _cd_lbl_days[idx] = dl;
}

// ─── Create the Countdown screen ─────────────────────────────────────────────
inline lv_obj_t* ui_countdown_create(AppPrefs* prefs) {
    _cd_prefs_ptr = prefs;

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(CD_CLR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _cd_scr = scr;

    // ── Header ──────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, 44);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(CD_CLR_HDR_BG), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_BELL "  Countdown  (tap a row to edit)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CD_CLR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    // ── Row list (COUNTDOWN_COUNT*CD_ROW_H fits in the viewport — no scroll
    //    needed at the default count/height, but kept scrollable for safety
    //    if either constant is changed) ───────────────────────────────────
    int list_h = SCREEN_HEIGHT - 44 - 30;
    _cd_list_panel = lv_obj_create(scr);
    lv_obj_set_size(_cd_list_panel, SCREEN_WIDTH, list_h);
    lv_obj_set_pos(_cd_list_panel, 0, 44);
    lv_obj_set_style_bg_color(_cd_list_panel, lv_color_hex(CD_CLR_BG), 0);
    lv_obj_set_style_border_width(_cd_list_panel, 0, 0);
    lv_obj_set_style_radius(_cd_list_panel, 0, 0);
    lv_obj_set_style_pad_all(_cd_list_panel, 0, 0);
    lv_obj_set_scroll_dir(_cd_list_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_cd_list_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(_cd_list_panel, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_cd_list_panel, lv_color_hex(0x333355), LV_PART_SCROLLBAR);

    for (int i = 0; i < COUNTDOWN_COUNT; i++)
        _cd_build_row(_cd_list_panel, i, i * CD_ROW_H);

    // ── Navigation bar ─────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_COUNTDOWN);

    for (int i = 0; i < COUNTDOWN_COUNT; i++)
        _cd_render_days(i);

    return scr;
}

// ─── Recompute all row displays (called after prefs_load() and on every
// navigateTo(SCR_COUNTDOWN) — dates may have rolled over since last shown) ──
inline void ui_countdown_refresh(AppPrefs* prefs) {
    if (prefs) _cd_prefs_ptr = prefs;
    if (!_cd_prefs_ptr) return;
    for (int i = 0; i < COUNTDOWN_COUNT; i++)
        _cd_render_days(i);
}
