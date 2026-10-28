#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// fetch_mgr.h — SSL heap recovery, per-source data fetchers, the first-boot
// fetch sequence, and auto-dim scheduling.
//
// Split out of main.cpp — see app_state.h for the broader rationale. Pure
// code motion, no behavior change.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "app_state.h"
#include "wifi_mgr.h"      // wifi_connect()
#include "ntp_mgr.h"       // ntp_sync()
#include "prefs_mgr.h"
#include "backlight.h"
#include "buzzer.h"
#include "weather_api.h"
#include "stock_api.h"
#include "iss_api.h"
#include "alerts_api.h"
#include "nfl_api.h"
#include "nba_api.h"
#include "ui_setup.h"      // ui_setup_set_status()
#include "ui_main.h"       // ui_main_update_weather/stocks()
#include "ui_stocks.h"
#include "ui_forecast.h"
#include "ui_hourly.h"
#include "ui_alert.h"
#include "ui_nfl.h"
#include "ui_nba.h"

// ─────────────────────────────────────────────────────────────────────────────
// SSL heap recovery — REACTIVE WiFi cycle, called only after a fetch fails.
//
// Previously this ran before every stocks/iss/alerts/NFL/NBA fetch.  Once the
// steady-state SRAM baseline settled below the 70 KB trigger (which it now
// does — see memory: ssl-heap-vs-screen-count), that meant a full WiFi
// teardown+reconnect (~2 s, WDT-risky) before *every* fetch, every round,
// with free SRAM barely moving afterward.  Pure tax, no benefit.
//
// It is now invoked only when a fetch actually returns failure, to give the
// next fetch in the same round a chance.  The >= 70 KB early-out also makes it
// a no-op when a fetch failed with plenty of SRAM free (i.e. a transient
// network error, not heap exhaustion).
//
// Root cause of SSL -32512 ("Memory allocation failed") after ~12 hours uptime:
//   mbedTLS allocates small structs directly from SRAM via heap_caps_calloc(MALLOC_CAP_INTERNAL),
//   bypassing SPIRAM_MALLOC_ALWAYSINTERNAL.  After ~386 SSL handshakes (stocks every 5 min
//   + alerts + weather + NFL/NBA over 12 h) SRAM fragments: total free may be 60+ KB
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

static void do_stocks_fetch() {
    esp_task_wdt_reset();
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
    if (!fetchIss(g_weather.latitude, g_weather.longitude, g_iss))
        recover_ssl_heap();   // reactive: give the next fetch in this round a chance
    ui_forecast_update_iss(g_iss);
    last_iss_ms = millis();
}

static void do_alerts_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected || g_weather.latitude == 0.0f) return;
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
                // Extreme: 8 rapid pulses (80ms on / 40ms off) → 400ms gap, for 5 s
                Serial.println("[BUZZER] Extreme alert — 5 s pulsing");
                buzzer_alert_pulse(80, 40, 8, 400, 5000);
            } else {
                // Severe: 5 medium pulses (120ms on / 80ms off) → 600ms gap, for 5 s
                Serial.println("[BUZZER] Severe alert — 5 s pulsing");
                buzzer_alert_pulse(120, 80, 5, 600, 5000);
            }
        }
        last_alert_hash = h;
    } else {
        recover_ssl_heap();   // reactive: give the next fetch in this round a chance
    }
    last_alerts_ms = millis();
}

static void do_nfl_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    Serial.println("[NFL] Fetching NFL schedule...");
    if (fetchNfl(g_nfl, g_prefs.utc_offset_sec)) {
        ui_nfl_update(g_nfl);
    } else {
        recover_ssl_heap();   // reactive: give the next fetch in this round a chance
    }
    last_nfl_ms = millis();
}

static void do_nba_fetch() {
    esp_task_wdt_reset();
    if (!wifi_connected) return;
    Serial.println("[NBA] Fetching NBA schedule...");
    if (fetchNba(g_nba, g_prefs.utc_offset_sec, g_prefs.nba_team1_id, g_prefs.nba_team2_id)) {
        ui_nba_update(g_nba);
    } else {
        recover_ssl_heap();   // reactive: give the next fetch in this round a chance
    }
    last_nba_ms = millis();
}

// Performs the full first-load fetch sequence with LVGL yielding between calls.
static void initial_fetch() {
    // After ESP.restart() (ESP_RST_SW), skip ISS/NFL/NBA to conserve SRAM.
    // Each SSL handshake leaves ~3-4 KB of non-reclaimable SRAM fragments.
    // ISS/NFL/NBA cost ~3 KB each.  Skipping all three keeps SRAM above ~65 KB
    // when stocks runs, ensuring the SHA hardware accelerator can complete
    // every TLS handshake in the batch.  Skipped APIs pick up on their normal
    // periodic intervals (NFL/NBA=1 hr, ISS=6 hr).
    bool sw_restart = (esp_reset_reason() == ESP_RST_SW);

    ui_setup_set_status("Fetching weather...", lv_color_hex(0x4fc3f7));
    lv_timer_handler(); delay(20);

    do_weather_fetch(/*force_geocode=*/true);

    if (sw_restart) {
        // On software restart: run stocks immediately after weather while SRAM is
        // freshest (only 1 prior SSL handshake).  Each additional handshake before
        // stocks further fragments the SRAM heap, leaving the SHA DMA allocator
        // without a contiguous block when Yahoo drops keep-alive mid-batch.
        // ISS / NFL / NBA are deferred to their periodic timers.
        ui_setup_set_status("Fetching market data...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_stocks_fetch();

        ui_setup_set_status("Checking weather alerts...", lv_color_hex(0x4fc3f7));
        lv_timer_handler(); delay(20);
        do_alerts_fetch();
    } else {
        // Normal (hardware) boot: full fetch sequence.
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
