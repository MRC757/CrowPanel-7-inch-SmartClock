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

static lv_obj_t*          _hourly_scr         = nullptr;
static lv_obj_t*          _hourly_hdr_time     = nullptr;
static lv_obj_t*          _hourly_day_lbl[3]  = {};

static lv_obj_t*          _chart_temp         = nullptr;
static lv_obj_t*          _chart_wind         = nullptr;
static lv_obj_t*          _chart_prec         = nullptr;
static lv_chart_series_t* _ser_temp           = nullptr;
static lv_chart_series_t* _ser_wind           = nullptr;
static lv_chart_series_t* _ser_prec           = nullptr;

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

// ─── Helper: create one chart row ─────────────────────────────────────────────
// y         — y position of the row label (chart sits _HLY_LBL_H below)
// color     — series line colour
// y_min/max — Y-axis range
// major_cnt — number of labelled tick marks (controls step size)
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
    lv_obj_set_size(ch, SCREEN_WIDTH, _HLY_CHART_H);
    lv_obj_set_pos(ch, 0, chart_y);

    // Background / border
    lv_obj_set_style_bg_color(ch, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 0, 0);
    // Left padding = reserved space for Y-axis tick labels.
    // Without this the series area starts at x=0 and LVGL draws the labels
    // at x<0 (off-screen). pad_left pushes the data area right so labels
    // land in the visible 0.._HLY_Y_AXIS_W strip.
    lv_obj_set_style_pad_left(ch, _HLY_Y_AXIS_W, 0);
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

    // Y-axis ticks with auto-generated numeric labels
    lv_chart_set_axis_tick(ch, LV_CHART_AXIS_PRIMARY_Y,
                           6, 0,           // major_len, minor_len
                           major_cnt, 0,   // major_cnt, minor_cnt
                           true,           // label_en
                           _HLY_Y_AXIS_W); // draw_size (pixels reserved for labels)
    lv_obj_set_style_line_color(ch, lv_color_hex(0x444466), LV_PART_TICKS);
    lv_obj_set_style_text_color(ch, lv_color_hex(0x9e9e9e), LV_PART_TICKS);
    lv_obj_set_style_text_font(ch, &lv_font_montserrat_12, LV_PART_TICKS);

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

    // ── Day-name row + boundary lines ─────────────────────────────────────
    // The chart data area starts at x ≈ _HLY_Y_AXIS_W (reserved for Y labels).
    // Day labels are centred at 1/6, 1/2, 5/6 of the remaining width.
    {
        const int data_w = SCREEN_WIDTH - _HLY_Y_AXIS_W;  // 760
        const int cx[3] = {
            _HLY_Y_AXIS_W + data_w / 6,           // ~167
            _HLY_Y_AXIS_W + data_w / 2,            // ~420
            _HLY_Y_AXIS_W + (data_w * 5) / 6      // ~673
        };

        for (int d = 0; d < 3; d++) {
            _hourly_day_lbl[d] = lv_label_create(scr);
            lv_label_set_text(_hourly_day_lbl[d], "---");
            lv_obj_set_style_text_font(_hourly_day_lbl[d], &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(_hourly_day_lbl[d], lv_color_hex(0x78c8f0), 0);
            // 60-px wide area centred on cx[d]
            lv_obj_set_size(_hourly_day_lbl[d], 60, _HLY_DAY_ROW_H);
            lv_obj_set_pos(_hourly_day_lbl[d], cx[d] - 30, _HLY_HDR_H + 2);
            lv_obj_set_style_text_align(_hourly_day_lbl[d], LV_TEXT_ALIGN_CENTER, 0);
        }

        // Vertical boundary lines at 1/3 and 2/3 of data area
        const int div_x[2] = {
            _HLY_Y_AXIS_W + data_w / 3,            // ~293
            _HLY_Y_AXIS_W + (data_w * 2) / 3       // ~547
        };
        // Span from top of first chart to bottom of last chart
        const int div_top = _HLY_ROW0_Y + _HLY_LBL_H;           // y=86
        const int div_bot = _HLY_ROW2_Y + _HLY_LBL_H + _HLY_CHART_H; // y=448

        for (int i = 0; i < 2; i++) {
            lv_obj_t* dv = lv_obj_create(scr);
            lv_obj_set_size(dv, 1, div_bot - div_top);
            lv_obj_set_pos(dv, div_x[i], div_top);
            lv_obj_set_style_bg_color(dv, lv_color_hex(0x3a3a5a), 0);
            lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dv, 0, 0);
            lv_obj_set_style_radius(dv, 0, 0);
            lv_obj_set_style_pad_all(dv, 0, 0);
            lv_obj_set_style_shadow_width(dv, 0, 0);
            lv_obj_clear_flag(dv, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    // ── Row labels ────────────────────────────────────────────────────────
    // Temperature
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "TEMPERATURE (\xB0" "F)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xff7043), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW0_Y + 2);
    }
    // Wind
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "WIND SPEED (mph)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x4dd0e1), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW1_Y + 2);
    }
    // Precip
    {
        lv_obj_t* lbl = lv_label_create(scr);
        lv_label_set_text(lbl, "PRECIP CHANCE (%)");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x4fc3f7), 0);
        lv_obj_set_pos(lbl, 6, _HLY_ROW2_Y + 2);
    }

    // ── Charts ────────────────────────────────────────────────────────────
    // Temperature: -10 to 110°F, 7 major ticks → 20° intervals
    _chart_temp = _make_hly_chart(scr, _HLY_ROW0_Y,
                                   lv_color_hex(0xff7043),
                                   -10, 110, 7,
                                   _h_temp, &_ser_temp);

    // Wind: 0 to 50 mph, 6 major ticks → 10 mph intervals
    _chart_wind = _make_hly_chart(scr, _HLY_ROW1_Y,
                                   lv_color_hex(0x4dd0e1),
                                   0, 50, 6,
                                   _h_wind, &_ser_wind);

    // Precip: 0 to 100%, 6 major ticks → 20% intervals
    _chart_prec = _make_hly_chart(scr, _HLY_ROW2_Y,
                                   lv_color_hex(0x4fc3f7),
                                   0, 100, 6,
                                   _h_prec, &_ser_prec);

    // ── Navigation bar ────────────────────────────────────────────────────
    _create_nav_bar(scr, SCR_HOURLY);

    return scr;
}

// ─── Update all three charts with new hourly weather data ─────────────────────
inline void ui_hourly_update(const WeatherData& wd)
{
    if (!_hourly_scr || !_chart_temp) return;
    if (!wd.valid || wd.hourly.count == 0) return;

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

    // Update day-name labels: today / tomorrow / day-after
    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        static const char* wdays[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
        for (int d = 0; d < 3; d++) {
            lv_label_set_text(_hourly_day_lbl[d], wdays[(ti.tm_wday + d) % 7]);
        }
    }
}

// ─── Refresh header timestamp (called on navigation) ─────────────────────────
inline void ui_hourly_tick()
{
    if (!_hourly_hdr_time) return;
    struct tm ti;
    if (getLocalTime(&ti, 100)) {
        char ts[20];
        strftime(ts, sizeof(ts), "%I:%M %p", &ti);
        // Strip leading zero from hour ("01:30 PM" → "1:30 PM")
        if (ts[0] == '0') memmove(ts, ts + 1, strlen(ts));
        lv_label_set_text(_hourly_hdr_time, ts);
    }
}
