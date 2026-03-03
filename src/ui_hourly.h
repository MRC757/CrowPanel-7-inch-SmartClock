#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui_hourly.h — 3-day hourly forecast chart screen
//
// Layout (800 × 480):
//   Header bar                          h=50   y=0
//   Day-name row  (Mon / Tue / Wed)     h=20   y=50
//   "TEMPERATURE (°F)" row label        h=16   y=70
//   Temperature line chart              h=110  y=86
//   "WIND SPEED (mph)" row label        h=16   y=196
//   Wind line chart                     h=110  y=212
//   "PRECIP CHANCE (%)" row label       h=16   y=322
//   Precip line chart                   h=110  y=338
//   (2 px gap)
//   Navigation bar                      h=30   y=450
//
// Fixed Y-axis ranges (user-specified):
//   Temperature: -10 to 110 °F  (7 ticks: -10,10,30,50,70,90,110 — 20° steps)
//   Wind:          0 to  50 mph (6 ticks:  0,10,20,30,40,50       — 10 mph steps)
//   Precip:        0 to 100 %   (6 ticks:  0,20,40,60,80,100      — 20% steps)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "config.h"
#include "weather_api.h"

static lv_obj_t*          _hourly_scr             = nullptr;
static lv_obj_t*          _hourly_hdr_time         = nullptr;
static lv_obj_t*          _hourly_day_lbl[3]       = {};
static unsigned long      _hourly_last_update_ms   = 0;   // millis() at last fetch

static lv_obj_t*          _chart_temp         = nullptr;
static lv_obj_t*          _chart_wind         = nullptr;
static lv_obj_t*          _chart_prec         = nullptr;
static lv_chart_series_t* _ser_temp           = nullptr;
static lv_chart_series_t* _ser_wind           = nullptr;
static lv_chart_series_t* _ser_prec           = nullptr;
// Starting hour of the hourly dataset (0-23); used by the Midnight/Noon markers.
static int _hourly_start_hour = 0;

// Midnight / Noon vertical marker lines and their time labels.
// [0-2] = midnight (blue), [3-5] = noon (gold); at most 3 of each in 72 hours.
static lv_obj_t* _hourly_vline[6]     = {};
static lv_obj_t* _hourly_vline_lbl[6] = {};

// Base X positions saved by ui_hourly_update(); used by ui_hourly_tick() to
// apply per-minute scroll offset without re-computing from raw data each tick.
// 0 = slot not in use (sentinel; valid positions are always >= _HLY_Y_AXIS_W).
static int _hourly_vline_base_x[6] = {};
static int _hourly_day_cx[3]       = {};

// Persistent data arrays — lv_chart reads these directly via ext_y_array.
// File-scope statics are zero-initialized at program start.
static lv_coord_t _h_temp[HOURLY_COUNT] = {};
static lv_coord_t _h_wind[HOURLY_COUNT] = {};
static lv_coord_t _h_prec[HOURLY_COUNT] = {};

// ─── Layout constants ─────────────────────────────────────────────────────────
static constexpr int _HLY_HDR_H     = 50;   // header bar height
static constexpr int _HLY_DAY_ROW_H = 20;   // day-name row height
static constexpr int _HLY_LBL_H     = 16;   // row label height
static constexpr int _HLY_CHART_H   = 110;  // chart height
static constexpr int _HLY_Y_AXIS_W  = 40;   // approx pixels for Y-axis (draw_size)

// Y position of each row's label (chart is _HLY_LBL_H below each)
static constexpr int _HLY_ROW0_Y = _HLY_HDR_H + _HLY_DAY_ROW_H;                 // 70
static constexpr int _HLY_ROW1_Y = _HLY_ROW0_Y + _HLY_LBL_H + _HLY_CHART_H;    // 196
static constexpr int _HLY_ROW2_Y = _HLY_ROW1_Y + _HLY_LBL_H + _HLY_CHART_H;    // 322

// ─── Y-axis labels as standalone lv_label objects ─────────────────────────────
// LVGL 8.3's LV_EVENT_DRAW_PART_BEGIN callback for tick labels draws OUTSIDE
// the chart widget's bounds; the parent clip sometimes swallows those draws.
// Creating separate lv_label children of scr guarantees they always render.
//
// chart_y = top y of chart widget (= row_y + _HLY_LBL_H)
// major_cnt ticks: 0=bottom (y_min), major_cnt-1=top (y_max)
// suffix: appended to value ("°" / "" / "%")
static void _add_y_axis_labels(lv_obj_t* scr, int chart_y,
                                int y_min, int y_max, int major_cnt,
                                const char* suffix)
{
    // Data area inside the chart widget (pad_top=4, pad_bottom=4)
    const int data_bottom = chart_y + _HLY_CHART_H - 4;  // last data pixel y
    const int data_h      = _HLY_CHART_H - 8;             // 102 px

    for (int i = 0; i < major_cnt; i++) {
        int value  = y_min + (y_max - y_min) * i / (major_cnt - 1);
        int y_tick = data_bottom - i * data_h / (major_cnt - 1);

        char buf[12];
        snprintf(buf, sizeof(buf), "%d%s", value, suffix ? suffix : "");

        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xc0c8d8), 0);
        // Right-align within the 36px strip to the left of the chart
        lv_obj_set_size(lbl, _HLY_Y_AXIS_W - 4, 14);
        lv_obj_set_pos(lbl, 2, y_tick - 7);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
    }
}

// ─── Helper: create one chart row ─────────────────────────────────────────────
// row_y     — y position of the row label (chart sits _HLY_LBL_H below)
// color     — series line colour
// y_min/max — Y-axis range
// major_cnt — number of tick marks / grid intervals
// ext_arr   — static array of HOURLY_COUNT lv_coord_t values
// ser_out   — receives the created series pointer
static lv_obj_t* _make_hly_chart(lv_obj_t* parent, int row_y,
                                   lv_color_t color,
                                   int y_min, int y_max, int major_cnt,
                                   lv_coord_t* ext_arr,
                                   lv_chart_series_t** ser_out)
{
    int chart_y = row_y + _HLY_LBL_H;

    lv_obj_t* ch = lv_chart_create(parent);
    lv_obj_set_size(ch, SCREEN_WIDTH - _HLY_Y_AXIS_W, _HLY_CHART_H);
    lv_obj_set_pos(ch, _HLY_Y_AXIS_W, chart_y);

    // Background / border
    lv_obj_set_style_bg_color(ch, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 0, 0);
    // Chart starts at x=_HLY_Y_AXIS_W. LVGL draws Y-axis tick labels OUTSIDE
    // the widget to the left (via ext_draw_size), so they land in screen x=0..40.
    lv_obj_set_style_pad_left(ch, 0, 0);
    lv_obj_set_style_pad_top(ch, 4, 0);
    lv_obj_set_style_pad_bottom(ch, 4, 0);
    lv_obj_set_style_pad_right(ch, 4, 0);
    lv_obj_clear_flag(ch, LV_OBJ_FLAG_SCROLLABLE);

    // Chart type and data count
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, HOURLY_COUNT);
    lv_chart_set_range(ch, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

    // Horizontal grid lines — one fewer than major ticks = one per interval
    lv_chart_set_div_line_count(ch, (uint8_t)(major_cnt - 1), 0);
    lv_obj_set_style_line_color(ch, lv_color_hex(0x1c2040), LV_PART_MAIN);
    lv_obj_set_style_line_width(ch, 1, LV_PART_MAIN);

    // Y-axis: 6px tick marks only. Numeric labels are standalone lv_label objects
    // (created by _add_y_axis_labels) so they always render at known coordinates.
    lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_Y,
                           6, 0,        // major_len, minor_len
                           major_cnt, 0,// major_cnt, minor_cnt
                           false,       // label_en=false; standalone labels used
                           0);          // draw_size=0

    // X-axis ticks: 13 visual marks (one every 6 hours), no text labels.
    // Time orientation is provided by the Midnight/Noon vertical line markers.
    lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_X,
                           4, 0,   // major_len=4px, no minor ticks
                           13, 0,  // 13 major ticks, one every 6 hours
                           false,  // label_en=false
                           0);     // draw_size=0

    // Tick mark colour (text colour unused — labels are standalone lv_labels)
    lv_obj_set_style_line_color(ch, lv_color_hex(0x444466), LV_PART_TICKS);

    // Series
    lv_chart_series_t* ser = lv_chart_add_series(ch, color,
                                                  LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_ext_y_array(ch, ser, ext_arr);

    // Line style: 2 px wide, no point dots
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(ch, 0, LV_PART_INDICATOR);

    *ser_out = ser;
    return ch;
}

// ─── Create the hourly screen ─────────────────────────────────────────────────
inline lv_obj_t* ui_hourly_create()
{
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    _hourly_scr = scr;

    // ── Header ────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_WIDTH, _HLY_HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, LV_SYMBOL_REFRESH "  3-Day Hourly Forecast");
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(0x4fc3f7), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    _hourly_hdr_time = lv_label_create(hdr);
    lv_label_set_text(_hourly_hdr_time, "--:-- --");
    lv_obj_set_style_text_font(_hourly_hdr_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_hourly_hdr_time, lv_color_hex(0x9e9e9e), 0);
    lv_obj_align(_hourly_hdr_time, LV_ALIGN_RIGHT_MID, -14, 0);

    // ── Day-name labels ────────────────────────────────────────────────────
    // Positions are computed dynamically in ui_hourly_update() once the data
    // start hour is known.  Midnight lines replace the old static boundaries.
    for (int d = 0; d < 3; d++) {
        _hourly_day_lbl[d] = lv_label_create(scr);
        lv_label_set_text(_hourly_day_lbl[d], "---");
        lv_obj_set_style_text_font(_hourly_day_lbl[d], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_hourly_day_lbl[d], lv_color_hex(0x78c8f0), 0);
        lv_obj_set_size(_hourly_day_lbl[d], 60, _HLY_DAY_ROW_H);
        lv_obj_set_pos(_hourly_day_lbl[d], 0, _HLY_HDR_H + 2);
        lv_obj_set_style_text_align(_hourly_day_lbl[d], LV_TEXT_ALIGN_CENTER, 0);
    }

    // ── Row labels ────────────────────────────────────────────────────────
    // Temperature
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "TEMP (\xB0" "F)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xff7043), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW0_Y + 2);
    }
    // Wind
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "WIND (mph)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x4dd0e1), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW1_Y + 2);
    }
    // Precip
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "PRECIP (%)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x4fc3f7), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW2_Y + 2);
    }

    // ── Charts + Y-axis labels ────────────────────────────────────────────
    // Temperature: -10 to 110°F, 7 major ticks → 20° intervals
    _chart_temp = _make_hly_chart(scr, _HLY_ROW0_Y,
                                   lv_color_hex(0xff7043),
                                   -10, 110, 7,
                                   _h_temp, &_ser_temp);
    _add_y_axis_labels(scr, _HLY_ROW0_Y + _HLY_LBL_H, -10, 110, 7, "\xB0");

    // Wind: 0 to 50 mph, 6 major ticks → 10 mph intervals
    _chart_wind = _make_hly_chart(scr, _HLY_ROW1_Y,
                                   lv_color_hex(0x4dd0e1),
                                   0, 50, 6,
                                   _h_wind, &_ser_wind);
    _add_y_axis_labels(scr, _HLY_ROW1_Y + _HLY_LBL_H, 0, 50, 6, "");

    // Precip: 0 to 100%, 6 major ticks → 20% intervals
    _chart_prec = _make_hly_chart(scr, _HLY_ROW2_Y,
                                   lv_color_hex(0x4fc3f7),
                                   0, 100, 6,
                                   _h_prec, &_ser_prec);
    _add_y_axis_labels(scr, _HLY_ROW2_Y + _HLY_LBL_H, 0, 100, 6, "%");

    // ── Midnight / Noon vertical markers ──────────────────────────────────
    // Created AFTER charts so they render on top (higher z-order).
    // x-position and visibility are set dynamically in ui_hourly_update().
    {
        const int vline_top = _HLY_ROW0_Y + _HLY_LBL_H;
        const int vline_h   = _HLY_ROW2_Y + _HLY_LBL_H + _HLY_CHART_H - vline_top; // 362
        for (int k = 0; k < 6; k++) {
            bool is_mid = (k < 3);  // [0-2]=midnight, [3-5]=noon

            lv_obj_t* vl = lv_obj_create(scr);
            lv_obj_set_size(vl, 1, vline_h);
            lv_obj_set_pos(vl, 0, vline_top);
            lv_obj_set_style_bg_color(vl,
                is_mid ? lv_color_hex(0x8899cc) : lv_color_hex(0xe8c468), 0);
            lv_obj_set_style_bg_opa(vl, LV_OPA_70, 0);
            lv_obj_set_style_border_width(vl, 0, 0);
            lv_obj_set_style_radius(vl, 0, 0);
            lv_obj_set_style_pad_all(vl, 0, 0);
            lv_obj_set_style_shadow_width(vl, 0, 0);
            lv_obj_clear_flag(vl, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(vl, LV_OBJ_FLAG_HIDDEN);
            _hourly_vline[k] = vl;

            // Small time label in the day-name row above each line
            lv_obj_t* lbl = lv_label_create(scr);
            lv_label_set_text(lbl, is_mid ? "12A" : "12P");
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(lbl,
                is_mid ? lv_color_hex(0x8899cc) : lv_color_hex(0xe8c468), 0);
            lv_obj_set_pos(lbl, 0, _HLY_ROW0_Y);
            lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
            _hourly_vline_lbl[k] = lbl;
        }
    }

    // ── Navigation bar ────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_HOURLY);

    return scr;
}

// ─── Update all three charts with new hourly weather data ─────────────────────
inline void ui_hourly_update(const WeatherData& wd)
{
    if (!_hourly_scr || !_chart_temp) return;
    if (!wd.valid || wd.hourly.count == 0) return;

    // Record fetch time and reset staleness color
    _hourly_last_update_ms = millis();
    if (_hourly_hdr_time)
        lv_obj_set_style_text_color(_hourly_hdr_time, lv_color_hex(0x9e9e9e), 0);

    // Fill static arrays from WeatherData
    int n = min(wd.hourly.count, HOURLY_COUNT);
    lv_coord_t lt = 0, lw = 0, lp = 0;
    for (int i = 0; i < n; i++) {
        lt = _h_temp[i] = (lv_coord_t)wd.hourly.temp_f[i];
        lw = _h_wind[i] = (lv_coord_t)wd.hourly.wind_mph[i];
        lp = _h_prec[i] = (lv_coord_t)wd.hourly.precip_pct[i];
    }
    // Pad remaining slots with last value (avoids flat-zero tail on the chart)
    for (int i = n; i < HOURLY_COUNT; i++) {
        _h_temp[i] = lt;
        _h_wind[i] = lw;
        _h_prec[i] = lp;
    }

    // Redraw — lv_chart_set_ext_y_array already stored the array pointer;
    // just call refresh after updating the contents.
    lv_chart_refresh(_chart_temp);
    lv_chart_refresh(_chart_wind);
    lv_chart_refresh(_chart_prec);

    // Update day labels and position Midnight / Noon vertical markers
    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        _hourly_start_hour = ti.tm_hour;

        // Data X range: chart at x=_HLY_Y_AXIS_W, pad_right=4
        //   → data spans x=40..796 = 756 px across 72 points (71 gaps)
        const int x0     = _HLY_Y_AXIS_W;
        const int data_w = SCREEN_WIDTH - 4 - x0;          // 756 px
        const int vl_top = _HLY_ROW0_Y + _HLY_LBL_H;      // 86
        const int vl_h   = _HLY_ROW2_Y + _HLY_LBL_H + _HLY_CHART_H - vl_top; // 362

        // Scan 72-hour window for midnight (abs_h==0) and noon (abs_h==12)
        int mi = 0, ni = 0;
        for (int i = 0; i < HOURLY_COUNT && (mi < 3 || ni < 3); i++) {
            int abs_h = (_hourly_start_hour + i) % 24;
            int x     = x0 + i * data_w / (HOURLY_COUNT - 1);

            if (abs_h == 0 && mi < 3) {
                _hourly_vline_base_x[mi] = x;
                lv_obj_set_pos(_hourly_vline[mi], x, vl_top);
                lv_obj_set_height(_hourly_vline[mi], vl_h);
                lv_obj_set_pos(_hourly_vline_lbl[mi], x - 10, _HLY_ROW0_Y);
                lv_obj_clear_flag(_hourly_vline[mi],     LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(_hourly_vline_lbl[mi], LV_OBJ_FLAG_HIDDEN);
                mi++;
            }
            if (abs_h == 12 && ni < 3) {
                _hourly_vline_base_x[3 + ni] = x;
                lv_obj_set_pos(_hourly_vline[3 + ni], x, vl_top);
                lv_obj_set_height(_hourly_vline[3 + ni], vl_h);
                lv_obj_set_pos(_hourly_vline_lbl[3 + ni], x - 10, _HLY_ROW0_Y);
                lv_obj_clear_flag(_hourly_vline[3 + ni],     LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(_hourly_vline_lbl[3 + ni], LV_OBJ_FLAG_HIDDEN);
                ni++;
            }
        }
        // Hide any unused slots and clear their base positions
        for (int k = mi; k < 3; k++) {
            _hourly_vline_base_x[k] = 0;
            lv_obj_add_flag(_hourly_vline[k],     LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_hourly_vline_lbl[k], LV_OBJ_FLAG_HIDDEN);
        }
        for (int k = ni; k < 3; k++) {
            _hourly_vline_base_x[3 + k] = 0;
            lv_obj_add_flag(_hourly_vline[3 + k],     LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_hourly_vline_lbl[3 + k], LV_OBJ_FLAG_HIDDEN);
        }

        // Day label d sits directly above noon marker ni=d.
        // Day name is the weekday of that noon's calendar day.
        static const char* wdays[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        int first_noon_idx = (12 - _hourly_start_hour + 24) % 24;
        for (int d = 0; d < 3; d++) {
            if (_hourly_vline_base_x[3 + d] == 0) {   // noon slot unused — safety guard
                lv_obj_add_flag(_hourly_day_lbl[d], LV_OBJ_FLAG_HIDDEN);
                _hourly_day_cx[d] = 0;
                continue;
            }
            int noon_idx   = first_noon_idx + d * 24;
            int day_offset = (_hourly_start_hour + noon_idx) / 24;
            lv_label_set_text(_hourly_day_lbl[d], wdays[(ti.tm_wday + day_offset) % 7]);
            int cx = _hourly_vline_base_x[3 + d];
            if (cx < x0 + 30) cx = x0 + 30;
            if (cx > SCREEN_WIDTH - 30) cx = SCREEN_WIDTH - 30;
            _hourly_day_cx[d] = cx;  // save unscrolled center for tick()
            lv_obj_set_pos(_hourly_day_lbl[d], cx - 30, _HLY_HDR_H + 2);
            lv_obj_clear_flag(_hourly_day_lbl[d], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ─── Refresh header timestamp + smooth scroll (called every second) ───────────
// Smooth scroll: the left edge of the data area always represents "now".
// One LVGL data point = one hour.  Within each hour we slide the chart left
// by (seconds_elapsed / 3600) × (data_width / 71) pixels so the transition
// is continuous.  The chart snaps back on the next ui_hourly_update() call
// (i.e. when fresh API data arrives at the top of the hour).
inline void ui_hourly_tick()
{
    if (!_hourly_hdr_time) return;
    struct tm ti;
    if (!getLocalTime(&ti, 100)) return;

    // ── Header clock + staleness indicator ────────────────────────────────
    char ts[20];
    strftime(ts, sizeof(ts), "%I:%M %p", &ti);
    if (ts[0] == '0') memmove(ts, ts + 1, strlen(ts));
    lv_label_set_text(_hourly_hdr_time, ts);

    if (_hourly_last_update_ms > 0) {
        unsigned long age = millis() - _hourly_last_update_ms;
        lv_color_t c;
        if      (age > 4UL * WEATHER_UPDATE_MS) c = lv_color_hex(0xf44336);  // red
        else if (age > 2UL * WEATHER_UPDATE_MS) c = lv_color_hex(0xff9800);  // orange
        else                                    c = lv_color_hex(0x9e9e9e);   // gray
        lv_obj_set_style_text_color(_hourly_hdr_time, c, 0);
    }

    if (!_chart_temp) return;   // charts not created yet

    // ── Scroll offset ─────────────────────────────────────────────────────
    // data_w = pixels from first to last data point (71 gaps × ~10.65 px)
    const int data_w    = SCREEN_WIDTH - 4 - _HLY_Y_AXIS_W;     // 756 px
    const int sec_in_h  = ti.tm_min * 60 + ti.tm_sec;            // 0..3599
    // Integer arithmetic: multiply first to avoid rounding to zero.
    const int scroll_px = (int)((int64_t)sec_in_h * data_w
                                / ((HOURLY_COUNT - 1) * 3600));  // 0..~10 px

    const int chart_x = _HLY_Y_AXIS_W - scroll_px;

    // ── Shift chart widgets ───────────────────────────────────────────────
    lv_obj_set_x(_chart_temp, chart_x);
    lv_obj_set_x(_chart_wind, chart_x);
    lv_obj_set_x(_chart_prec, chart_x);

    // ── Shift Midnight / Noon markers ─────────────────────────────────────
    for (int k = 0; k < 6; k++) {
        if (_hourly_vline_base_x[k] == 0) continue;  // slot unused
        int vx = _hourly_vline_base_x[k] - scroll_px;
        lv_obj_set_x(_hourly_vline[k],     vx);
        lv_obj_set_x(_hourly_vline_lbl[k], vx - 10);
    }

    // ── Shift day-name labels ─────────────────────────────────────────────
    for (int d = 0; d < 3; d++) {
        if (_hourly_day_cx[d] == 0) continue;
        int cx = _hourly_day_cx[d] - scroll_px;
        if (cx < _HLY_Y_AXIS_W + 30) cx = _HLY_Y_AXIS_W + 30;
        if (cx > SCREEN_WIDTH - 30)   cx = SCREEN_WIDTH - 30;
        lv_obj_set_pos(_hourly_day_lbl[d], cx - 30, _HLY_HDR_H + 2);
    }
}
