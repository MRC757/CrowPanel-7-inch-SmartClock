#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_alert_overlay.h — Full-screen alert detail modal
//
// Shows comprehensive alert information when user taps the alert banner.
// Modal floats on lv_layer_top(), positioned center-screen with margins.
// Displays current alert (index _overlay_current_alert) out of ad.count total.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "alerts_api.h"

static lv_obj_t* _overlay_bg         = nullptr;
static lv_obj_t* _overlay_panel      = nullptr;
static lv_obj_t* _overlay_severity   = nullptr;
static lv_obj_t* _overlay_event      = nullptr;
static lv_obj_t* _overlay_desc       = nullptr;
static lv_obj_t* _overlay_instr      = nullptr;
static lv_obj_t* _overlay_area       = nullptr;
static lv_obj_t* _overlay_timing     = nullptr;
static lv_obj_t* _overlay_nav        = nullptr;
static lv_obj_t* _overlay_prev_btn   = nullptr;
static lv_obj_t* _overlay_next_btn   = nullptr;
static int       _overlay_current_alert = 0;
static bool      _overlay_visible    = false;
static AlertsData* _overlay_alert_data = nullptr;

// Forward declarations
static void _overlay_prev_click_cb(lv_event_t* e);
static void _overlay_next_click_cb(lv_event_t* e);
static void _overlay_close_click_cb(lv_event_t* e);
static void _overlay_bg_click_cb(lv_event_t* e);


// ─── Initialize the modal (call after lv_disp_drv_register) ──────────────────
inline void ui_alert_overlay_init() {
    lv_obj_t* layer = lv_layer_top();

    // Semi-transparent background — click to dismiss
    _overlay_bg = lv_obj_create(layer);
    lv_obj_set_size(_overlay_bg, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(_overlay_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_overlay_bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_overlay_bg, 0, 0);
    lv_obj_add_flag(_overlay_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_overlay_bg, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Main panel — 600×340 centered (100px margin on sides, 70px top/bottom)
    _overlay_panel = lv_obj_create(layer);
    lv_obj_set_size(_overlay_panel, 600, 340);
    lv_obj_set_pos(_overlay_panel, 100, 70);
    lv_obj_set_style_bg_color(_overlay_panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(_overlay_panel, lv_color_hex(0xffd700), 0);
    lv_obj_set_style_border_width(_overlay_panel, 2, 0);
    lv_obj_set_style_radius(_overlay_panel, 8, 0);
    lv_obj_set_style_pad_all(_overlay_panel, 12, 0);
    lv_obj_set_scrollbar_mode(_overlay_panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);

    // ── Header: Severity badge + Event + Close button (row) ────────────────────
    lv_obj_t* hdr = lv_obj_create(_overlay_panel);
    lv_obj_set_size(hdr, 576, 30);
    lv_obj_set_style_bg_opa(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Severity badge (color-coded)
    _overlay_severity = lv_label_create(hdr);
    lv_label_set_text(_overlay_severity, "");
    lv_obj_set_style_text_font(_overlay_severity, &lv_font_montserrat_14, 0);
    lv_obj_set_size(_overlay_severity, 80, 24);
    lv_obj_set_style_bg_color(_overlay_severity, lv_color_hex(0xff6b6b), 0);
    lv_obj_set_style_bg_opa(_overlay_severity, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_overlay_severity, 4, 0);
    lv_obj_set_style_pad_all(_overlay_severity, 2, 0);
    lv_obj_set_style_text_align(_overlay_severity, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(_overlay_severity, lv_color_white(), 0);

    // Event type (large, bold)
    _overlay_event = lv_label_create(hdr);
    lv_label_set_text(_overlay_event, "");
    lv_obj_set_style_text_font(_overlay_event, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_overlay_event, lv_color_hex(0xffd700), 0);
    lv_obj_set_flex_grow(_overlay_event, 1);

    // Close button (X) — clickable label
    lv_obj_t* close_btn = lv_label_create(hdr);
    lv_label_set_text(close_btn, "✕");
    lv_obj_set_style_text_font(close_btn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(close_btn, lv_color_hex(0xffffff), 0);

    // ── Content sections ──────────────────────────────────────────────────────
    // Description
    _overlay_desc = lv_label_create(_overlay_panel);
    lv_label_set_text(_overlay_desc, "");
    lv_obj_set_style_text_font(_overlay_desc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_overlay_desc, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_width(_overlay_desc, 576);
    lv_label_set_long_mode(_overlay_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(_overlay_desc, 4, 0);

    // Instruction
    _overlay_instr = lv_label_create(_overlay_panel);
    lv_label_set_text(_overlay_instr, "");
    lv_obj_set_style_text_font(_overlay_instr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_overlay_instr, lv_color_hex(0xffd700), 0);
    lv_obj_set_width(_overlay_instr, 576);
    lv_label_set_long_mode(_overlay_instr, LV_LABEL_LONG_WRAP);

    // Area
    _overlay_area = lv_label_create(_overlay_panel);
    lv_label_set_text(_overlay_area, "");
    lv_obj_set_style_text_font(_overlay_area, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_overlay_area, lv_color_hex(0x888888), 0);
    lv_obj_set_width(_overlay_area, 576);
    lv_label_set_long_mode(_overlay_area, LV_LABEL_LONG_WRAP);

    // Timing
    _overlay_timing = lv_label_create(_overlay_panel);
    lv_label_set_text(_overlay_timing, "");
    lv_obj_set_style_text_font(_overlay_timing, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_overlay_timing, lv_color_hex(0x888888), 0);
    lv_obj_set_width(_overlay_timing, 576);
    lv_label_set_long_mode(_overlay_timing, LV_LABEL_LONG_WRAP);

    // ── Footer: Navigation buttons ────────────────────────────────────────────
    _overlay_nav = lv_obj_create(_overlay_panel);
    lv_obj_set_size(_overlay_nav, 576, 32);
    lv_obj_set_style_bg_opa(_overlay_nav, 0, 0);
    lv_obj_set_style_border_width(_overlay_nav, 0, 0);
    lv_obj_set_flex_flow(_overlay_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_overlay_nav, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Previous button
    _overlay_prev_btn = lv_btn_create(_overlay_nav);
    lv_obj_set_size(_overlay_prev_btn, 80, 28);
    lv_obj_t* prev_lbl = lv_label_create(_overlay_prev_btn);
    lv_label_set_text(prev_lbl, "◀ Prev");
    lv_obj_set_style_text_font(prev_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(_overlay_prev_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(_overlay_prev_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(_overlay_prev_btn, 1, 0);

    // Alert counter
    lv_obj_t* count_lbl = lv_label_create(_overlay_nav);
    lv_label_set_text(count_lbl, "1/1");
    lv_obj_set_style_text_font(count_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_flex_grow(count_lbl, 1);

    // Next button
    _overlay_next_btn = lv_btn_create(_overlay_nav);
    lv_obj_set_size(_overlay_next_btn, 80, 28);
    lv_obj_t* next_lbl = lv_label_create(_overlay_next_btn);
    lv_label_set_text(next_lbl, "Next ▶");
    lv_obj_set_style_text_font(next_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_color(_overlay_next_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(_overlay_next_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_width(_overlay_next_btn, 1, 0);

    // Wire up event handlers
    lv_obj_add_event_cb(_overlay_prev_btn, _overlay_prev_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(_overlay_next_btn, _overlay_next_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(_overlay_bg, _overlay_bg_click_cb, LV_EVENT_CLICKED, nullptr);
}

// ─── Format a Unix timestamp to "Day H:MM AM/PM" local time ────────────────────
static void _format_alert_time(time_t t, char* buf, size_t sz) {
    if (t == 0) {
        snprintf(buf, sz, "--:--");
        return;
    }
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    char tbuf[20];
    strftime(tbuf, sizeof(tbuf), "%a %I:%M %p", &tm_info);
    // Remove leading zero from hour (e.g., "05:30 AM" → "5:30 AM")
    if (tbuf[4] == '0') memmove(tbuf + 4, tbuf + 5, strlen(tbuf + 5) + 1);
    snprintf(buf, sz, "%s", tbuf);
}

// ─── Set severity badge color based on severity string ──────────────────────────
static lv_color_t _severity_color(const char* severity) {
    if (strcmp(severity, "Extreme") == 0)
        return lv_color_hex(0xff0000);  // bright red
    if (strcmp(severity, "Severe") == 0)
        return lv_color_hex(0xff6b6b);  // red
    if (strcmp(severity, "Moderate") == 0)
        return lv_color_hex(0xffa500);  // orange
    return lv_color_hex(0xffeb3b);      // yellow (Minor/Unknown)
}

// ─── Show the modal with details of alert at _overlay_current_alert ────────────
inline void ui_alert_overlay_show(const AlertsData& ad) {
    if (!_overlay_panel || ad.count == 0) return;
    _overlay_alert_data = (AlertsData*)&ad;
    if (_overlay_current_alert >= ad.count) _overlay_current_alert = 0;

    const AlertInfo& a = ad.alerts[_overlay_current_alert];

    // Severity badge (color-coded)
    lv_color_t badge_color = _severity_color(a.severity);
    lv_obj_set_style_bg_color(_overlay_severity, badge_color, 0);
    lv_label_set_text(_overlay_severity, a.severity);

    // Event type
    lv_label_set_text(_overlay_event, a.event);

    // Description (multi-line)
    if (strlen(a.description) > 0) {
        lv_label_set_text(_overlay_desc, a.description);
    } else {
        lv_label_set_text(_overlay_desc, "No details available.");
    }

    // Instruction label hidden (field removed to conserve memory)
    lv_label_set_text(_overlay_instr, "");

    // Area hidden (field removed — alerts already scoped to user's zip code)
    lv_label_set_text(_overlay_area, "");

    // Timing (onset → expires)
    char onset_str[32], expires_str[32];
    _format_alert_time(a.onset, onset_str, sizeof(onset_str));
    _format_alert_time(a.expires, expires_str, sizeof(expires_str));
    String timing_text = "";
    timing_text += "Active: ";
    timing_text += onset_str;
    timing_text += " → ";
    timing_text += expires_str;
    lv_label_set_text(_overlay_timing, timing_text.c_str());

    // Update nav buttons and counter
    // Find the counter label in the nav container
    lv_obj_t* nav_child = lv_obj_get_child(_overlay_nav, 1);  // 2nd child (count label)
    if (nav_child) {
        String counter = "";
        counter += (_overlay_current_alert + 1);
        counter += "/";
        counter += ad.count;
        lv_label_set_text(nav_child, counter.c_str());
    }

    // Enable/disable previous button
    lv_obj_t* prev_btn = lv_obj_get_child(_overlay_nav, 0);
    if (_overlay_current_alert == 0) {
        lv_obj_add_state(prev_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(prev_btn, LV_STATE_DISABLED);
    }

    // Enable/disable next button
    lv_obj_t* next_btn = lv_obj_get_child(_overlay_nav, 2);
    if (_overlay_current_alert >= ad.count - 1) {
        lv_obj_add_state(next_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(next_btn, LV_STATE_DISABLED);
    }

    // Show modal
    lv_obj_clear_flag(_overlay_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
    _overlay_visible = true;
}

// ─── Hide the modal ────────────────────────────────────────────────────────────
inline void ui_alert_overlay_hide() {
    if (_overlay_panel) {
        lv_obj_add_flag(_overlay_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_overlay_panel, LV_OBJ_FLAG_HIDDEN);
        _overlay_visible = false;
    }
}

// ─── Navigate to previous alert ────────────────────────────────────────────────
inline void ui_alert_overlay_prev(const AlertsData& ad) {
    if (_overlay_current_alert > 0) {
        _overlay_current_alert--;
        ui_alert_overlay_show(ad);
    }
}

// ─── Navigate to next alert ────────────────────────────────────────────────────
inline void ui_alert_overlay_next(const AlertsData& ad) {
    if (_overlay_current_alert < ad.count - 1) {
        _overlay_current_alert++;
        ui_alert_overlay_show(ad);
    }
}

// ─── Check if modal is currently visible ───────────────────────────────────────
inline bool ui_alert_overlay_is_visible() {
    return _overlay_visible;
}

// ─── Event handlers (defined after functions) ────────────────────────────────
static void _overlay_prev_click_cb(lv_event_t* e) {
    if (_overlay_alert_data) {
        ui_alert_overlay_prev(*_overlay_alert_data);
    }
}

static void _overlay_next_click_cb(lv_event_t* e) {
    if (_overlay_alert_data) {
        ui_alert_overlay_next(*_overlay_alert_data);
    }
}

static void _overlay_close_click_cb(lv_event_t* e) {
    ui_alert_overlay_hide();
}

static void _overlay_bg_click_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ui_alert_overlay_hide();
    }
}
