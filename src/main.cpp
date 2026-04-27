// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — Smart Clock for CrowPanel Advance 7" HMI (ESP32-S3, 800×480)
//
// Boot flow:
//   1. Hardware init (I2C @ 400 kHz, backlight via STC8H MCU, display, GT911 touch)
//   2. LVGL init + register display/touch drivers
//   3. Build all screens (Setup, Clock, News, Stocks, Forecast, Hourly, NFL, NBA, Joke)
//   4. Load NVS preferences (WiFi, ZIP, stock symbols/names, NBA team IDs)
//      - If WiFi credentials exist  → try connecting, then go to Clock screen
//      - Otherwise                  → show Setup screen
//   5. Main loop: lv_timer_handler + periodic data fetches
//
// Data refresh schedule (configurable in config.h):
//   Weather + forecast + UV  — every  1 hr   (Open-Meteo, no key)
//   Weather alerts           — every  5 min  (NWS, US only, no key)
//   News                     — every 30 min  (Google News RSS, no key)
//   Stocks                   — every  5 min  (Yahoo Finance chart API, no key)
//   NFL                      — every  1 hr   (Ball Don't Lie, free key required)
//   NBA                      — every  1 hr   (Ball Don't Lie, same key as NFL)
//   ISS pass times           — every  6 hr   (N2YO, free key required)
//   NTP re-sync              — every  1 hr
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>

#include <esp_task_wdt.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <lvgl.h>
#include <time.h>

#include "LGFX_Driver.h"   // defines LGFX gfx
#include "config.h"
#include "backlight.h"     // software brightness (RGB565 pixel scaling)
#include "buzzer.h"        // piezo buzzer via STC8H I2C 0x30
#include "rtc_bm8563.h"   // BM8563 hardware RTC at I2C 0x51 (battery-backed)
#include "prefs_mgr.h"
#include "weather_api.h"
#include "news_api.h"
#include "stock_api.h"
#include "moon.h"
#include "iss_api.h"
#include "alerts_api.h"
#include "ui_setup.h"
#include "ui_main.h"
#include "ui_news.h"
#include "ui_stocks.h"
#include "ui_forecast.h"
#include "ui_hourly.h"
#include "ui_alert.h"
#include "nfl_api.h"
#include "ui_nfl.h"
#include "nba_api.h"
#include "ui_nba.h"

// GT911 touch controller (TAMC_GT911 via Wire; SDA=15, SCL=16, no IRQ/RST pins)
static TAMC_GT911 ts(I2C_SDA_PIN, I2C_SCL_PIN, -1, -1, SCREEN_WIDTH, SCREEN_HEIGHT);

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────
AppPrefs    g_prefs;
WeatherData g_weather = {};
StocksData  g_stocks  = {};
NewsData    g_news    = {};
IssData     g_iss     = {};
AlertsData  g_alerts  = {};
NflData     g_nfl     = {};
NbaData     g_nba     = {};

// Active LVGL screen objects
static lv_obj_t* scr_setup    = nullptr;
static lv_obj_t* scr_main     = nullptr;
static lv_obj_t* scr_news     = nullptr;
static lv_obj_t* scr_stocks   = nullptr;
static lv_obj_t* scr_forecast = nullptr;
static lv_obj_t* scr_hourly   = nullptr;
static lv_obj_t* scr_nfl      = nullptr;
static lv_obj_t* scr_nba      = nullptr;

// Timing
static unsigned long last_weather_ms   = 0;
static unsigned long last_news_ms      = 0;
static unsigned long last_stocks_ms    = 0;
static unsigned long last_ntp_ms       = 0;
static unsigned long last_iss_ms       = 0;
static unsigned long last_alerts_ms    = 0;
static unsigned long last_nfl_ms            = 0;
static unsigned long last_nba_ms            = 0;
static unsigned long last_dim_check_ms      = 0;  // auto-dim evaluation interval
static unsigned long last_reconnect_ms      = 0;  // WiFi reconnect watchdog

// Alert state — track a hash of active Extreme/Severe events so we only
// buzz when the alert set genuinely changes (not on every 5-min re-fetch).
static uint32_t      last_alert_hash   = 0;

// State flags
static bool wifi_connected   = false;
static bool first_fetch_done = false;
static bool rtc_available    = false;   // true if BM8563 responds on I2C

// ─────────────────────────────────────────────────────────────────────────────
// Backlight — controlled by STC8H1K28 MCU at I2C address 0x30.
// Wire must be initialized before calling this.
// ─────────────────────────────────────────────────────────────────────────────
static void backlight_init() {
    // 0x10 = max brightness for both v1.2 and v1.3 (confirmed by Elecrow PlatformIO example).
    Wire.beginTransmission(0x30);
    Wire.write(0x10);
    Wire.endTransmission();
    Serial.println("[HW] Backlight ON via 0x30");
}

// ─────────────────────────────────────────────────────────────────────────────
// LVGL — display flush callback
// ─────────────────────────────────────────────────────────────────────────────
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* lvgl_buf1 = nullptr;

static void disp_flush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* px_map) {
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    // Scale pixel brightness in-place before pushing to display.
    // backlight_scale_buf() is a no-op when brightness == 100%.
    backlight_scale_buf((uint16_t*)px_map, w * h);

    // Write one scanline at a time inside matched startWrite/endWrite pairs.
    //
    // With use_psram=2 (double-buffered), pushImageDMA writes to the BACK
    // framebuffer via the ESP32-S3 D-cache (synchronous CPU memcpy SRAM→PSRAM).
    // The D-cache is 32 KB — exactly the size of a 20-scanline LVGL strip.
    // Without endWrite(), dirty D-cache lines are never flushed to PSRAM,
    // so the LCD_CAM GDMA reads stale pixels from the front buffer.
    //
    // Each endWrite() calls cacheWriteBack(), which flushes only the currently
    // dirty D-cache lines.  After a single 800-pixel row is written those are
    // ~1.6 KB (25 cache lines of 64 B).  Flushing per-row keeps every burst
    // small and avoids the 32 KB single-shot stall that was causing jitter.
    //
    // endWrite() also calls display() → sets the Panel_RGB vsync flip flag
    // (a boolean, so idempotent across multiple calls within one frame).
    // The front/back framebuffers are swapped atomically at the next vsync.
    //
    // The 30 µs gap between rows gives the LCD_CAM GDMA time to read from
    // the front buffer before the PSRAM bus is hit again by a cache writeback.
    auto* px = (uint16_t*)px_map;
    for (int row = 0; row < h; row++) {
        gfx.startWrite();
        gfx.pushImageDMA(area->x1, area->y1 + row, w, 1, px + row * w);
        gfx.endWrite();
        if (row < h - 1) delayMicroseconds(30);
    }

    lv_disp_flush_ready(disp);
}

// ─────────────────────────────────────────────────────────────────────────────
// LVGL — touch input callback (TAMC_GT911 via Wire)
// ─────────────────────────────────────────────────────────────────────────────
static void touch_read(lv_indev_drv_t* indev, lv_indev_data_t* data) {
    ts.read();
    if (ts.isTouched) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = ts.points[0].x;
        data->point.y = ts.points[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LVGL — periodic clock tick timer (fires every 1 second inside lv_timer_handler)
// ─────────────────────────────────────────────────────────────────────────────
static void clock_tick_cb(lv_timer_t* /*t*/) {
    ui_main_update_clock();
}

// ─────────────────────────────────────────────────────────────────────────────
// navigateTo() — referenced by all nav-bar buttons via config.h extern
// ─────────────────────────────────────────────────────────────────────────────
void navigateTo(int screenId) {
    lv_obj_t* target = nullptr;
    switch (screenId) {
        case SCR_SETUP:  target = scr_setup;  break;
        case SCR_MAIN:   target = scr_main;   break;
        case SCR_NEWS:
            target = scr_news;
            ui_news_tick();          // refresh staleness color
            break;
        case SCR_STOCKS:
            target = scr_stocks;
            ui_stocks_update(g_stocks);  // push latest data
            break;
        case SCR_FORECAST:
            target = scr_forecast;
            ui_forecast_tick();
            break;
        case SCR_HOURLY:
            target = scr_hourly;
            ui_hourly_tick();
            break;
        case SCR_NFL:
            target = scr_nfl;
            ui_nfl_tick();
            break;
        case SCR_NBA:
            target = scr_nba;
            ui_nba_tick();
            break;
        default: return;
    }
    if (target) {
        lv_scr_load(target);   // instant switch — animations cause multi-frame flushes that tear on Panel_RGB
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WiFi helpers
// ─────────────────────────────────────────────────────────────────────────────
// Non-blocking-friendly connect: yields to LVGL every 100 ms while waiting.
static bool wifi_connect(const char* ssid, const char* pass,
                          unsigned long timeout_ms = 15000) {
    if (strlen(ssid) == 0) return false;

    Serial.printf("[WiFi] Connecting to \"%s\" ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeout_ms) {
        lv_timer_handler();   // keep LVGL alive (spinner animation, etc.)
        delay(100);
    }

    wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected) {
        Serial.printf("[WiFi] Connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Connection timed out");
        WiFi.disconnect();
    }
    return wifi_connected;
}

// ─────────────────────────────────────────────────────────────────────────────
// SSL heap recovery — proactive WiFi cycle when SRAM is critically fragmented.
//
// Root cause of SSL -32512 ("Memory allocation failed") after ~12 hours uptime:
//   mbedTLS allocates small structs directly from SRAM via heap_caps_calloc(MALLOC_CAP_INTERNAL),
//   bypassing SPIRAM_MALLOC_ALWAYSINTERNAL.  After ~386 SSL handshakes (stocks every 5 min
//   + alerts + weather + news + NFL/NBA over 12 h) SRAM fragments: total free may be 60+ KB
//   but no contiguous block exists for a new mbedtls_ssl_handshake_params (~3-4 KB).
//
//   heap_caps_get_free_size() is O(1) per region — safe to call.  Threshold is 70 KB
//   (not 40 KB) because total free does NOT equal largest contiguous block; at lower
//   values the heap is already fragmented beyond what WiFi cycling can recover.
//
//   NOTE: WiFi cycling does NOT defragment SRAM.  cert-parse fragments persist across
//   connect/disconnect cycles.  If the proactive cycle fails to prevent -32512, the
//   reactive path in do_stocks_fetch() forces a second cycle then calls ESP.restart().
// ─────────────────────────────────────────────────────────────────────────────
static void recover_ssl_heap() {
    uint32_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    // Note: get_free_size() returns TOTAL free, not largest contiguous block.
    // The threshold is set conservatively high (70 KB) so WiFi is cycled while
    // SRAM fragmentation is still mild — large contiguous blocks still exist.
    // At lower values (e.g. 40 KB), total free can be spread across tiny fragments
    // with no block large enough for a new SSL context, causing -32512 alloc failure.
    if (free_sram >= 70000) return;

    Serial.printf("[SSL] Low SRAM (%u B) — cycling WiFi to recover heap\n", free_sram);
    wifi_connected = false;
    WiFi.disconnect(true);    // release DHCP lease and free LwIP/driver allocations
    delay(1000);              // allow LwIP teardown to complete before reconnect
    if (wifi_connect(g_prefs.wifi_ssid, g_prefs.wifi_pass)) {
        Serial.printf("[SSL] Reconnected — free SRAM now %u B\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NTP sync — uses UTC offset stored in g_prefs (or returned by Open-Meteo).
// Calling configTime() with the correct total offset gives local time via
// getLocalTime(), no POSIX TZ string required.
// ─────────────────────────────────────────────────────────────────────────────
static void ntp_sync() {
    if (!wifi_connected) return;
    // Pass totalOffset as gmtOffset, 0 daylight — Open-Meteo already folds DST in.
    configTime(g_prefs.utc_offset_sec, 0, NTP_SERVER_1, NTP_SERVER_2);
    Serial.printf("[NTP] Sync requested (UTC%+d)\n", g_prefs.utc_offset_sec / 3600);
    last_ntp_ms = millis();

    // Wait briefly for SNTP response, then write the accurate time to the
    // BM8563 hardware RTC so it survives power cycles.
    if (rtc_available) {
        struct tm ti;
        for (int attempt = 0; attempt < 10; attempt++) {
            delay(200);
            if (getLocalTime(&ti, 100)) {
                rtc_write(ti);   // BM8563 stores local time directly
                break;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Data fetch helpers — each yields to LVGL during the HTTP request
// ─────────────────────────────────────────────────────────────────────────────
static void do_weather_fetch(bool force_geocode = false) {
    esp_task_wdt_reset();
    if (!wifi_connected || strlen(g_prefs.zip_code) == 0) return;
    Serial.println("[WX] Fetching weather...");
    if (fetchWeather(g_prefs.zip_code, g_weather, force_geocode)) {
        // If the timezone offset changed, re-sync NTP
        if (g_weather.utc_offset_sec != g_prefs.utc_offset_sec) {
            g_prefs.utc_offset_sec = g_weather.utc_offset_sec;
            prefs_save_utc(g_prefs.utc_offset_sec);
            ntp_sync();
        }
        // Persist city name
        if (strcmp(g_weather.city_name, g_prefs.city_name) != 0) {
            strncpy(g_prefs.city_name, g_weather.city_name, sizeof(g_prefs.city_name) - 1);
            prefs_save_city(g_prefs.city_name);
        }
        ui_main_update_weather(g_weather);
        delay(1);  // yield to scheduler — feed interrupt WDT between UI updates
        ui_forecast_update(g_weather);
        delay(1);
        ui_hourly_update(g_weather);
    }
    last_weather_ms = millis();
}

static void do_news_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    Serial.println("[NEWS] Fetching news...");
    fetchNews(g_news);
    ui_main_update_news(g_news);
    ui_news_update(g_news);
    // If fetch completely failed (timeout/SSL error), retry in 5 min instead of 30.
    last_news_ms = g_news.valid ? millis() : millis() - NEWS_UPDATE_MS + 5UL * 60 * 1000;
}

static void do_stocks_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    recover_ssl_heap();          // proactive: cycle WiFi if SRAM below 70 KB threshold
    if (!wifi_connected) return;
    Serial.println("[STOCKS] Fetching stocks...");
    if (!fetchStocks(g_stocks, g_prefs.stock_symbols, g_prefs.stock_names)) {
        // All 6 symbols failed — SSL alloc failure due to SRAM heap fragmentation.
        // WiFi cycling does NOT defragment SRAM (mbedTLS cert-parse fragments persist
        // across connect/disconnect cycles).  Try one WiFi cycle as a cheap first
        // attempt, then fall back to esp_restart() if SRAM is still critically low.
        // esp_restart() is the correct fix: BM8563 RTC preserves time, NVS preserves
        // credentials, and the device recovers in ~10 s with a clean unfragmented heap.
        uint32_t pre_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        Serial.printf("[STOCKS] All failed (free SRAM %u B) — cycling WiFi...\n", pre_sram);
        wifi_connected = false;
        WiFi.disconnect(true);
        delay(1000);
        if (wifi_connect(g_prefs.wifi_ssid, g_prefs.wifi_pass)) {
            uint32_t post_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            Serial.printf("[SSL] Reconnected — free SRAM now %u B\n", post_sram);
            if (!fetchStocks(g_stocks, g_prefs.stock_symbols, g_prefs.stock_names)) {
                if (post_sram < 65000) {
                    // SRAM fragmented beyond WiFi-cycle recovery; restart is required.
                    Serial.println("[SSL] Heap unrecoverable — restarting in 3 s");
                    delay(3000);
                    ESP.restart();
                }
                // else: SRAM looks adequate → transient network issue; skip this cycle
            }
        }
    }
    ui_main_update_stocks(g_stocks);
    ui_stocks_update(g_stocks);
    last_stocks_ms = millis();
}

static void do_iss_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected || g_weather.latitude == 0.0f) return;
    Serial.println("[ISS] Fetching visible pass times...");
    if (fetchIss(g_weather.latitude, g_weather.longitude, g_iss)) {
        ui_forecast_update_iss(g_iss);
    }
    last_iss_ms = millis();
}

static void do_alerts_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected || g_weather.latitude == 0.0f) return;
    recover_ssl_heap();   // defensive: alerts runs right after stocks; guard against same fragmentation
    if (!wifi_connected) return;
    Serial.println("[ALERTS] Fetching weather alerts...");
    if (fetchAlerts(g_weather.latitude, g_weather.longitude, g_alerts)) {
        ui_alert_update(g_alerts);

        // Compute a fingerprint of all active Extreme/Severe alert events.
        // djb2-style hash over event strings lets us detect new or changed alerts
        // without storing a full list, and ignores re-fetches of the same alert.
        uint32_t h = 0;
        bool has_extreme = false;
        bool has_severe  = false;
        for (int i = 0; i < g_alerts.count; i++) {
            const char* sev = g_alerts.alerts[i].severity;
            if (strcmp(sev, "Extreme") == 0) has_extreme = true;
            if (strcmp(sev, "Severe")  == 0) has_severe  = true;
            if (strcmp(sev, "Extreme") == 0 || strcmp(sev, "Severe") == 0) {
                const char* ev = g_alerts.alerts[i].event;
                while (*ev) h = h * 31u + (uint8_t)*ev++;
            }
        }

        // Only buzz when a new Extreme/Severe alert set arrives (h changed to non-zero).
        if (h != 0 && h != last_alert_hash) {
            if (has_extreme) {
                Serial.println("[BUZZER] Extreme alert — 5 beeps");
                buzzer_beep(5, 150, 100);
            } else {
                Serial.println("[BUZZER] Severe alert — 3 beeps");
                buzzer_beep(3, 200, 150);
            }
        }
        last_alert_hash = h;
    }
    last_alerts_ms = millis();
}

static void do_nfl_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    Serial.println("[NFL] Fetching NFL schedule...");
    if (fetchNfl(g_nfl, g_prefs.utc_offset_sec)) {
        ui_nfl_update(g_nfl);
    }
    last_nfl_ms = millis();
}

static void do_nba_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    Serial.println("[NBA] Fetching NBA schedule...");
    if (fetchNba(g_nba, g_prefs.utc_offset_sec, g_prefs.nba_team1_id, g_prefs.nba_team2_id)) {
        ui_nba_update(g_nba);
    }
    last_nba_ms = millis();
}

// Performs the full first-load fetch sequence with LVGL yielding between calls.
static void initial_fetch() {
    // After ESP.restart() (ESP_RST_SW), skip news + ISS/NFL/NBA to conserve SRAM.
    // Each SSL handshake leaves ~3-4 KB of non-reclaimable SRAM fragments.
    // News costs ~7 KB; ISS/NFL/NBA cost ~3 KB each.  Skipping all four keeps
    // SRAM above ~65 KB when stocks runs, ensuring the SHA hardware accelerator
    // can complete every TLS handshake in the batch.  Skipped APIs pick up on
    // their normal periodic intervals (news=30 min, NFL/NBA=1 hr, ISS=6 hr).
    bool sw_restart = (esp_reset_reason() == ESP_RST_SW);

    ui_setup_set_status("Fetching weather...", lv_color_hex(0x4fc3f7));
    lv_timer_handler(); delay(20);

    do_weather_fetch(/*force_geocode=*/true);

    if (sw_restart) {
        // On software restart: run stocks immediately after weather while SRAM is
        // freshest (only 1 prior SSL handshake).  Each additional handshake before
        // stocks further fragments the SRAM heap, leaving the SHA DMA allocator
        // without a contiguous block when Yahoo drops keep-alive mid-batch.
        // News / ISS / NFL / NBA are deferred to their periodic timers.
        ui_setup_set_status("Fetching market data...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_stocks_fetch();

        ui_setup_set_status("Checking weather alerts...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_alerts_fetch();
    } else {
        // Normal (hardware) boot: full fetch sequence.
        ui_setup_set_status("Fetching news...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_news_fetch();

        ui_setup_set_status("Fetching market data...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_stocks_fetch();

        ui_setup_set_status("Checking weather alerts...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_alerts_fetch();

        ui_setup_set_status("Fetching ISS pass times...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_iss_fetch();

        ui_setup_set_status("Fetching NFL schedule...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_nfl_fetch();

        ui_setup_set_status("Fetching NBA schedule...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_nba_fetch();
    }

    first_fetch_done = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-dim — compare current local time against today's sunrise/sunset.
// Runs every 60 seconds in loop() and once after initial_fetch().
// ─────────────────────────────────────────────────────────────────────────────
static void check_auto_dim() {
    if (!g_weather.valid) return;   // no sunrise/sunset data yet

    struct tm ti;
    if (!getLocalTime(&ti, 100)) return;

    int cur_min  = ti.tm_hour * 60 + ti.tm_min;
    int rise_min = g_weather.sunrise_hour  * 60 + g_weather.sunrise_minute;
    int set_min  = g_weather.sunset_hour   * 60 + g_weather.sunset_minute;

    bool    is_day = (cur_min >= rise_min && cur_min < set_min);
    uint8_t target = is_day ? 100 : g_prefs.night_brightness;

    if (target != backlight_get_pct()) {
        backlight_set_pct(target);
        // Force LVGL to redraw the active screen so all pixels reflect the new level
        lv_obj_invalidate(lv_scr_act());
        Serial.printf("[BRIGHT] %s → %d%% (rise %02d:%02d, set %02d:%02d)\n",
                      is_day ? "Day" : "Night", (int)target,
                      g_weather.sunrise_hour, g_weather.sunrise_minute,
                      g_weather.sunset_hour,  g_weather.sunset_minute);
    }
    last_dim_check_ms = millis();
}

// Called by the setup-screen brightness slider
static void on_night_brightness_change(uint8_t pct) {
    g_prefs.night_brightness = pct;
    prefs_save_brightness(pct);
    check_auto_dim();  // apply immediately if currently in night hours
}

// Called when a stock symbol/name is changed on the setup screen.
static void on_stock_changed(int idx, const char* sym, const char* name) {
    strncpy(g_prefs.stock_symbols[idx], sym,  sizeof(g_prefs.stock_symbols[idx]) - 1);
    strncpy(g_prefs.stock_names[idx],   name, sizeof(g_prefs.stock_names[idx])   - 1);
    prefs_save_stock(idx, sym, name);
    Serial.printf("[SETUP] Stock %d saved: %s (%s)\n", idx, sym, name);
}

// Called when either NBA team dropdown changes on the setup screen.
static void on_teams_changed(int team1_id, int team2_id) {
    g_prefs.nba_team1_id = team1_id;
    g_prefs.nba_team2_id = team2_id;
    prefs_save_nba_teams(team1_id, team2_id);
    Serial.printf("[SETUP] NBA teams saved: %d / %d\n", team1_id, team2_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup screen callback — called when user presses "Scan WiFi"
// ─────────────────────────────────────────────────────────────────────────────
static void on_scan_networks() {
    ui_setup_set_status("Scanning for WiFi networks...", lv_color_hex(0xffb300));
    lv_timer_handler();   // render the status update before blocking

    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();   // blocking ~2 s; safe on setup screen

    if (n <= 0) {
        WiFi.scanDelete();
        ui_setup_set_status("No networks found. Try again.", lv_color_hex(0xf44336));
        return;
    }

    // Copy results into the struct array that ui_setup_show_scan_results expects
    int m = (n > 20) ? 20 : n;
    static WifiNetwork nets[20];
    for (int i = 0; i < m; i++) {
        String s = WiFi.SSID(i);
        strncpy(nets[i].ssid, s.c_str(), sizeof(nets[i].ssid) - 1);
        nets[i].ssid[sizeof(nets[i].ssid) - 1] = '\0';
        nets[i].rssi    = WiFi.RSSI(i);
        nets[i].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    ui_setup_show_scan_results(nets, m);
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup screen callback — called when user presses "Connect & Save"
// ─────────────────────────────────────────────────────────────────────────────
static void on_setup_connect(const char* ssid, const char* pass, const char* zip) {
    // Validate inputs
    if (strlen(ssid) == 0) {
        ui_setup_set_status("Error: WiFi network name cannot be empty.",
                            lv_color_hex(0xf44336));
        return;
    }
    if (strlen(zip) < 5) {
        ui_setup_set_status("Error: ZIP code must be 5 digits.",
                            lv_color_hex(0xf44336));
        return;
    }

    ui_setup_set_status("Connecting to WiFi...", lv_color_hex(0xffb300));
    lv_timer_handler();

    // Attempt WiFi connection
    if (!wifi_connect(ssid, pass)) {
        ui_setup_set_status("WiFi connection failed. Check credentials and try again.",
                            lv_color_hex(0xf44336));
        return;
    }

    ui_setup_set_status("Connected! Saving settings...", lv_color_hex(0x4caf50));
    lv_timer_handler();

    // Save to NVS
    strncpy(g_prefs.wifi_ssid, ssid, sizeof(g_prefs.wifi_ssid) - 1);
    strncpy(g_prefs.wifi_pass, pass, sizeof(g_prefs.wifi_pass) - 1);
    strncpy(g_prefs.zip_code,  zip,  sizeof(g_prefs.zip_code)  - 1);
    prefs_save(g_prefs);

    // NTP sync then initial data fetch
    ntp_sync();
    initial_fetch();
    check_auto_dim();  // set correct brightness once weather data is ready

    // Switch to the main clock screen
    navigateTo(SCR_MAIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arduino setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] Smart Clock starting...");

    // ── Hardware task watchdog (30 s) ─────────────────────────────────────
    // Resets the device if the main task hangs in a blocking HTTP call
    // (e.g. server accepts the TCP connection but never sends a response).
    // 30 s covers the longest legitimate fetch (news: 20 s HTTP timeout + margin).
    // The WDT is fed at the top of loop() and before every blocking fetch.
    {
        const esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms    = 30000,
            .idle_core_mask = 0,
            .trigger_panic  = true,
        };
        if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK)
            esp_task_wdt_init(&wdt_cfg);
        esp_task_wdt_add(NULL);
    }

    // ── 1. I2C bus + touch + backlight ───────────────────────────────────
    // Wire.begin() before gfx.init() — LovyanGFX no longer owns I2C (Touch_GT911
    // removed from LGFX_Driver.h), so there is no conflict.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);  // 400 kHz (fast mode) — reduces GT911 touch read from ~1ms to ~250us,
                            // keeping I2C bus time within the Panel_RGB DMA bounce buffer window
    delay(50);   // v1.3: allow STC8H (0x30) and GT911 (0x5D) to settle on I2C bus
    ts.begin(GT911_ADDR1);              // 0x5D confirmed by I2C probe
    ts.setRotation(ROTATION_INVERTED);  // raw GT911 coords match display orientation
    rtc_available = rtc_init();         // BM8563 at 0x51 (battery-backed RTC)
    backlight_init();
    // Boot beep — confirms buzzer hardware is operational.
    buzzer_on();
    delay(120);
    buzzer_off();
    Serial.println("[HW] I2C, GT911 touch, BM8563 RTC, backlight, and buzzer initialized");

    // ── 2. Display (Panel_RGB DMA) ────────────────────────────────────────
    gfx.init();
    // initDMA() is a no-op for Panel_FrameBufferBase (Panel_RGB); omitted.
    // No persistent startWrite() here — fillScreen() manages its own transaction,
    // and disp_flush() uses per-scanline startWrite/endWrite pairs.
    gfx.fillScreen(TFT_BLACK);
    Serial.println("[HW] Display initialized (800x480 RGB)");

    // ── 5. LVGL init ──────────────────────────────────────────────────────
    lv_init();

    // Single LVGL render buffer: 10 scan lines × 800 px × 2 B = 16 KB.
    // A single buffer is safe because disp_flush() is fully synchronous —
    // it doesn't return until all rows are pushed, so LVGL never writes into
    // the buffer while a flush is in progress.  The second 32 KB buffer is
    // only useful for async-DMA flush, which Panel_RGB doesn't support.
    // Keeping the buffer in SRAM keeps CPU compositing off the PSRAM bus.
    size_t buf_px    = (size_t)SCREEN_WIDTH * 10;   // 10 scan lines per strip
    size_t buf_bytes = buf_px * sizeof(lv_color_t);
    lvgl_buf1 = (lv_color_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!lvgl_buf1) {
        Serial.println("[LVGL] SRAM allocation failed — halting");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, lvgl_buf1, nullptr, buf_px);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_WIDTH;
    disp_drv.ver_res  = SCREEN_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Register touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("[LVGL] Initialized");

    // ── 6. Build all screens ──────────────────────────────────────────────
    // (UI modules use global navigateTo() for nav-bar buttons)
    scr_setup    = ui_setup_create(nullptr, on_setup_connect, on_night_brightness_change,
                                   on_scan_networks, on_stock_changed, on_teams_changed);
    scr_main     = ui_main_create();
    scr_news     = ui_news_create();
    scr_stocks   = ui_stocks_create();
    scr_forecast = ui_forecast_create();
    scr_hourly   = ui_hourly_create();
    scr_nfl      = ui_nfl_create();
    scr_nba      = ui_nba_create();
    ui_alert_init();   // floating banner on lv_layer_top(), above all screens

    Serial.println("[UI] All screens created");

    // ── 7. Load NVS preferences ───────────────────────────────────────────
    bool have_creds = prefs_load(g_prefs);
    // Pre-fill setup screen with any saved values
    ui_setup_refresh(&g_prefs);

    // Restore time from BM8563 hardware RTC.  The battery-backed RTC keeps
    // accurate local time across power cycles — no NVS epoch fallback needed.
    if (rtc_available) {
        rtc_restore_system_time(g_prefs.utc_offset_sec);
    }

    // Configure timezone so getLocalTime() applies the correct local offset
    // even before WiFi connects.  Also arms the SNTP client in the background;
    // it will sync automatically once WiFi comes up.
    configTime(g_prefs.utc_offset_sec, 0, NTP_SERVER_1, NTP_SERVER_2);
    Serial.printf("[RTC] Timezone configured: UTC%+ds\n", g_prefs.utc_offset_sec);

    // ── 8. LVGL recurring timer: update clock display every second ────────
    lv_timer_create(clock_tick_cb, 1000, nullptr);

    // ── 9. Initial screen & optional auto-connect ─────────────────────────
    if (have_creds) {
        // Show the clock face immediately while we connect in the background
        lv_scr_load(scr_main);
        lv_timer_handler();  // render one frame

        ui_setup_set_status("Auto-connecting to WiFi...", lv_color_hex(0xffb300));

        if (wifi_connect(g_prefs.wifi_ssid, g_prefs.wifi_pass)) {
            ntp_sync();
            initial_fetch();
            check_auto_dim();  // set correct brightness once weather data is ready
        } else {
            // Connection failed — drop back to setup so user can fix credentials
            ui_setup_set_status(
                "Auto-connect failed. Please check your WiFi settings.",
                lv_color_hex(0xf44336));
            lv_scr_load(scr_setup);
        }
    } else {
        lv_scr_load(scr_setup);
        Serial.println("[UI] Showing setup screen (no saved credentials)");
    }

    Serial.println("[BOOT] Setup complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// Arduino loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    esp_task_wdt_reset();

    // ── LVGL processing (must run frequently for animations, touch, etc.) ──
    lv_timer_handler();

    // ── Periodic data refresh (only when WiFi is up) ──────────────────────
    if (wifi_connected) {
        unsigned long now = millis();

        // Check WiFi is still alive
        if (WiFi.status() != WL_CONNECTED) {
            wifi_connected = false;
            Serial.println("[WiFi] Connection lost — will retry next interval");
        }

        // NTP re-sync
        if (now - last_ntp_ms >= NTP_SYNC_MS) {
            ntp_sync();
        }

        // Weather update
        if (first_fetch_done && now - last_weather_ms >= WEATHER_UPDATE_MS) {
            do_weather_fetch();
        }

        // News update
        if (first_fetch_done && now - last_news_ms >= NEWS_UPDATE_MS) {
            do_news_fetch();
        }

        // Stocks update
        if (first_fetch_done && now - last_stocks_ms >= STOCKS_UPDATE_MS) {
            do_stocks_fetch();
        }

        // Weather alerts update (5 min — safety-critical)
        if (first_fetch_done && now - last_alerts_ms >= ALERTS_UPDATE_MS) {
            do_alerts_fetch();
        }

        // ISS pass-time update (6 hours — passes are predictable)
        if (first_fetch_done && now - last_iss_ms >= ISS_UPDATE_MS) {
            do_iss_fetch();
        }

        // NFL schedule update (1 hour)
        if (first_fetch_done && now - last_nfl_ms >= NFL_UPDATE_MS) {
            do_nfl_fetch();
        }

        // NBA schedule update (1 hour)
        if (first_fetch_done && now - last_nba_ms >= NBA_UPDATE_MS) {
            do_nba_fetch();
        }

        // Auto-dim: re-evaluate day/night brightness every minute
        if (first_fetch_done && now - last_dim_check_ms >= 60000UL) {
            check_auto_dim();
        }

    } else if (!wifi_connected && first_fetch_done &&
               strlen(g_prefs.wifi_ssid) > 0 &&
               millis() - last_reconnect_ms >= 30000UL) {
        // Reconnect watchdog: retry every 30 s after a disconnect.
        // Uses a dedicated timer so no data-fetch timer is corrupted.
        last_reconnect_ms = millis();
        Serial.println("[WiFi] Attempting reconnect...");
        wifi_connect(g_prefs.wifi_ssid, g_prefs.wifi_pass, 8000);
        if (wifi_connected) {
            unsigned long now = millis();
            ntp_sync();
            // Re-fetch any source whose data is stale (age >= its normal interval)
            if (now - last_weather_ms >= WEATHER_UPDATE_MS) do_weather_fetch();
            if (now - last_news_ms    >= NEWS_UPDATE_MS)    do_news_fetch();
            if (now - last_stocks_ms  >= STOCKS_UPDATE_MS)  do_stocks_fetch();
            if (now - last_alerts_ms  >= ALERTS_UPDATE_MS)  do_alerts_fetch();
            if (now - last_nfl_ms     >= NFL_UPDATE_MS)     do_nfl_fetch();
            if (now - last_nba_ms     >= NBA_UPDATE_MS)     do_nba_fetch();
            // ISS is 6-hour; only refetch if genuinely stale
            if (now - last_iss_ms     >= ISS_UPDATE_MS)     do_iss_fetch();
        }
    }

    delay(5);  // ~5 ms gives LVGL ~200 Hz service rate; ample for 30 fps UI
}
