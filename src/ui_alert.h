#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_alert.h — Weather alert banner (NWS active alerts)
//
// Uses lv_layer_top() so the banner floats above ALL screens without
// modifying each screen's layout. The banner is hidden when there are
// no active alerts and automatically shown when alerts are present.
// Tapping the banner opens a full-screen modal with detailed alert info.
//
// Layout (800 x 26, y=0):
//   [⚠ ALERT]  scrolling event + headline text ... (click to expand)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "alerts_api.h"

static lv_obj_t* _alert_banner = nullptr;
static lv_obj_t* _alert_lbl    = nullptr;
static AlertsData* _alert_data_ptr = nullptr;  // pointer for tap event handler

// ─── Event handler: banner tap opens the alert overlay ────────────────────────
static void _alert_banner_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED && _alert_data_ptr && _alert_data_ptr->count > 0) {
        // Forward declaration requires actual include at compile time.
        // These will be resolved in main.cpp after ui_alert_overlay.h is included.
        extern void ui_alert_overlay_show(const AlertsData& ad);
        ui_alert_overlay_show(*_alert_data_ptr);
    }
}

// ─── Initialise the banner once (call after lv_disp_drv_register) ────────────
inline void ui_alert_init() {
    lv_obj_t* layer = lv_layer_top();

    _alert_banner = lv_obj_create(layer);
    lv_obj_set_size(_alert_banner, SCREEN_WIDTH, 26);
    lv_obj_set_pos(_alert_banner, 0, 0);
    lv_obj_set_style_bg_color(_alert_banner, lv_color_hex(0xb71c1c), 0);  // deep red
    lv_obj_set_style_bg_opa(_alert_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_alert_banner, 0, 0);
    lv_obj_set_style_radius(_alert_banner, 0, 0);
    lv_obj_set_style_pad_left(_alert_banner, 90, 0);
    lv_obj_set_style_pad_top(_alert_banner, 6, 0);
    lv_obj_clear_flag(_alert_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_alert_banner, LV_OBJ_FLAG_HIDDEN);  // hidden until alerts arrive
    lv_obj_add_flag(_alert_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_alert_banner, _alert_banner_event_cb, LV_EVENT_CLICKED, nullptr);

    // Fixed "⚠ ALERT" prefix on the left
    lv_obj_t* icon = lv_label_create(_alert_banner);
    lv_label_set_text(icon, LV_SYMBOL_WARNING " ALERT");
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xffeb3b), 0);  // yellow
    lv_obj_set_pos(icon, -84, 0);

    // Scrolling text for event names and headlines
    _alert_lbl = lv_label_create(_alert_banner);
    lv_label_set_text(_alert_lbl, "");
    lv_obj_set_style_text_font(_alert_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_alert_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(_alert_lbl, SCREEN_WIDTH - 96);
    lv_label_set_long_mode(_alert_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_speed(_alert_lbl, 40, 0);  // pixels/second
    lv_obj_set_pos(_alert_lbl, 0, 0);
}

// ─── Update banner visibility and content from latest alert data ─────────────
inline void ui_alert_update(const AlertsData& ad) {
    if (!_alert_banner || !_alert_lbl) return;

    // Store pointer for tap event handler
    _alert_data_ptr = (AlertsData*)&ad;

    if (!ad.valid || ad.count == 0) {
        lv_obj_add_flag(_alert_banner, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Build scrolling text: "Event: Headline  |  Event: Headline  |  ..."
    String text = "";
    for (int i = 0; i < ad.count; i++) {
        if (i > 0) text += "     |     ";
        text += ad.alerts[i].event;
        if (strlen(ad.alerts[i].headline) > 0) {
            text += ": ";
            text += ad.alerts[i].headline;
        }
    }
    lv_label_set_text(_alert_lbl, text.c_str());
    lv_obj_clear_flag(_alert_banner, LV_OBJ_FLAG_HIDDEN);
}
