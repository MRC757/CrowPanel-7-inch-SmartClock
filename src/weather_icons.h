#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// weather_icons.h — Procedural weather icons for the 5-day forecast cards
//
// Icons are composed of styled lv_obj_t children (filled rounded rectangles)
// placed inside a 50×50 transparent container. No canvas, no image files.
//
// Public API:
//   weather_icon_draw(lv_obj_t* cont, int wmo_code)
//     Cleans the container then draws the icon for the given WMO weather code.
//     Call after lv_obj_clean(cont) or on a fresh container.
//
// WMO code → icon mapping:
//   0        → sun (clear)
//   1–2      → partly cloudy
//   3        → cloudy / overcast
//   4–48     → fog / haze
//   49–55    → drizzle
//   56–65    → rain
//   66–77    → snow
//   80–82    → rain showers  (→ rain icon)
//   83–86    → snow showers  (→ snow icon)
//   95–99    → thunderstorm
// ─────────────────────────────────────────────────────────────────────────────
#include <lvgl.h>

// ─── Color palette ────────────────────────────────────────────────────────────
#define WI_YELLOW   0xFFCC00   // sun body and rays
#define WI_CLOUD_L  0xBBBBBB   // normal cloud
#define WI_CLOUD_D  0x5A6A78   // storm cloud (darker)
#define WI_BLUE     0x64B5F6   // rain drops
#define WI_WHITE    0xDDDDDD   // snow dots
#define WI_BOLT     0xFFE082   // lightning bolt
#define WI_FOG      0x8EA0AA   // fog bars

// ─── Helper: create one filled rounded-rect child ─────────────────────────────
static void _wi_rect(lv_obj_t* p,
                     int x, int y, int w, int h, int r,
                     uint32_t color) {
    lv_obj_t* o = lv_obj_create(p);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// ─── Cloud base (shared by rain, snow, storm, drizzle) ───────────────────────
static void _wi_cloud(lv_obj_t* cont, uint32_t clr) {
    _wi_rect(cont,  4,  2, 22, 18, 9, clr);   // left puff
    _wi_rect(cont, 16,  6, 20, 16, 8, clr);   // right puff (overlaps)
    _wi_rect(cont,  2, 14, 46, 18, 9, clr);   // wide base
}

// ─── Icon drawing functions ───────────────────────────────────────────────────

// Clear sky — yellow sun with 8 rays
static void _wi_draw_sun(lv_obj_t* cont) {
    // Rays first (behind body)
    _wi_rect(cont, 21,  0,  8,  7, 3, WI_YELLOW);   // N
    _wi_rect(cont, 21, 43,  8,  7, 3, WI_YELLOW);   // S
    _wi_rect(cont, 43, 21,  7,  8, 3, WI_YELLOW);   // E
    _wi_rect(cont,  0, 21,  7,  8, 3, WI_YELLOW);   // W
    _wi_rect(cont, 36,  4,  7,  7, 3, WI_YELLOW);   // NE
    _wi_rect(cont,  7,  4,  7,  7, 3, WI_YELLOW);   // NW
    _wi_rect(cont, 36, 39,  7,  7, 3, WI_YELLOW);   // SE
    _wi_rect(cont,  7, 39,  7,  7, 3, WI_YELLOW);   // SW
    // Sun body (on top of rays)
    _wi_rect(cont, 10, 10, 30, 30, 15, WI_YELLOW);
}

// Partly cloudy — small sun (upper-right) + cloud (lower-left)
static void _wi_draw_partly_cloudy(lv_obj_t* cont) {
    // Sun body (upper-right, no rays — space is tight)
    _wi_rect(cont, 24,  2, 22, 22, 11, WI_YELLOW);
    // Cloud puffs overlapping sun lower-left
    _wi_rect(cont,  2, 18, 20, 18,  9, WI_CLOUD_L);   // left puff
    _wi_rect(cont, 12, 22, 20, 16,  8, WI_CLOUD_L);   // right puff
    _wi_rect(cont,  0, 28, 46, 18,  9, WI_CLOUD_L);   // wide base
}

// Cloudy / overcast — three layered cloud puffs, no sun
static void _wi_draw_cloudy(lv_obj_t* cont) {
    _wi_rect(cont,  4,  8, 24, 22, 11, WI_CLOUD_L);   // left puff
    _wi_rect(cont, 18, 12, 22, 20, 10, WI_CLOUD_L);   // right puff
    _wi_rect(cont,  2, 22, 46, 22, 11, WI_CLOUD_L);   // wide base
}

// Fog / haze — three horizontal bars, staggered
static void _wi_draw_fog(lv_obj_t* cont) {
    _wi_rect(cont,  4, 10, 42, 7, 3, WI_FOG);
    _wi_rect(cont, 10, 22, 36, 7, 3, WI_FOG);
    _wi_rect(cont,  4, 34, 42, 7, 3, WI_FOG);
}

// Drizzle — light cloud + 3 small blue drops
static void _wi_draw_drizzle(lv_obj_t* cont) {
    _wi_cloud(cont, WI_CLOUD_L);
    _wi_rect(cont, 13, 36,  5, 10, 3, WI_BLUE);
    _wi_rect(cont, 23, 39,  5, 10, 3, WI_BLUE);
    _wi_rect(cont, 33, 36,  5, 10, 3, WI_BLUE);
}

// Rain — cloud + 4 heavier blue drops
static void _wi_draw_rain(lv_obj_t* cont) {
    _wi_cloud(cont, WI_CLOUD_L);
    _wi_rect(cont,  8, 36,  5, 12, 3, WI_BLUE);
    _wi_rect(cont, 18, 38,  5, 12, 3, WI_BLUE);
    _wi_rect(cont, 28, 36,  5, 12, 3, WI_BLUE);
    _wi_rect(cont, 38, 38,  5, 12, 3, WI_BLUE);
}

// Snow — cloud + 4 white dot clusters
static void _wi_draw_snow(lv_obj_t* cont) {
    _wi_cloud(cont, WI_CLOUD_L);
    _wi_rect(cont, 10, 37, 6, 6, 3, WI_WHITE);   // dot 1
    _wi_rect(cont, 20, 40, 6, 6, 3, WI_WHITE);   // dot 2
    _wi_rect(cont, 30, 37, 6, 6, 3, WI_WHITE);   // dot 3
    _wi_rect(cont, 38, 40, 6, 6, 3, WI_WHITE);   // dot 4
}

// Thunderstorm — dark cloud + offset yellow rectangles (lightning bolt)
static void _wi_draw_storm(lv_obj_t* cont) {
    _wi_cloud(cont, WI_CLOUD_D);
    // Lightning bolt: two rectangles offset left-to-right forming a Z/zigzag
    _wi_rect(cont, 22, 32, 14, 8, 2, WI_BOLT);   // upper part (right-side)
    _wi_rect(cont, 14, 40, 14, 8, 2, WI_BOLT);   // lower part (left-side)
}

// ─── WMO code → category mapper ──────────────────────────────────────────────
enum _WiCat {
    _WI_SUN = 0,
    _WI_PARTLY,
    _WI_CLOUDY,
    _WI_FOG,
    _WI_DRIZZLE,
    _WI_RAIN,
    _WI_SNOW,
    _WI_STORM
};

static int _wmo_to_cat(int code) {
    if (code == 0)           return _WI_SUN;
    if (code <= 2)           return _WI_PARTLY;
    if (code == 3)           return _WI_CLOUDY;
    if (code <= 48)          return _WI_FOG;
    if (code <= 55)          return _WI_DRIZZLE;
    if (code <= 65)          return _WI_RAIN;
    if (code <= 77)          return _WI_SNOW;
    if (code <= 82)          return _WI_RAIN;     // rain showers
    if (code <= 86)          return _WI_SNOW;     // snow showers
    return _WI_STORM;                             // 95–99 thunderstorm
}

// ─── Public API ───────────────────────────────────────────────────────────────
// Call after lv_obj_clean(cont). Container must be 50×50 with no padding.
static void weather_icon_draw(lv_obj_t* cont, int wmo_code) {
    switch (_wmo_to_cat(wmo_code)) {
        case _WI_SUN:     _wi_draw_sun(cont);          break;
        case _WI_PARTLY:  _wi_draw_partly_cloudy(cont); break;
        case _WI_CLOUDY:  _wi_draw_cloudy(cont);        break;
        case _WI_FOG:     _wi_draw_fog(cont);           break;
        case _WI_DRIZZLE: _wi_draw_drizzle(cont);       break;
        case _WI_RAIN:    _wi_draw_rain(cont);           break;
        case _WI_SNOW:    _wi_draw_snow(cont);           break;
        case _WI_STORM:   _wi_draw_storm(cont);          break;
        default:          _wi_draw_cloudy(cont);         break;
    }
}
