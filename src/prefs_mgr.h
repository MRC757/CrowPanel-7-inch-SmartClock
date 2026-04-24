#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// prefs_mgr.h  — Persistent NVS storage via Arduino Preferences library
// Stores: WiFi SSID, WiFi password, ZIP code, city name, UTC offset,
//         night brightness, stock symbols/names, NBA team IDs.
// ─────────────────────────────────────────────────────────────────────────────
#include <Preferences.h>
#include <string.h>
#include "config.h"   // STOCK_COUNT, STOCK_SYMBOLS_DEFAULT, STOCK_NAMES_DEFAULT

struct AppPrefs {
    char    wifi_ssid[64];
    char    wifi_pass[64];
    char    zip_code[8];
    char    city_name[64];
    int     utc_offset_sec;
    uint8_t night_brightness;          // 10–100 %; applied from sunset to sunrise

    // Configurable stocks (set via setup screen; defaults from config.h)
    char stock_symbols[STOCK_COUNT][12];   // e.g. "^GSPC"
    char stock_names[STOCK_COUNT][24];     // e.g. "S&P 500"

    // Configurable NBA teams (Ball Don't Lie team IDs; defaults Lakers=14, Warriors=10)
    int  nba_team1_id;
    int  nba_team2_id;
};

static Preferences _nvs;

// Load all preferences from NVS. Returns true if WiFi credentials exist.
inline bool prefs_load(AppPrefs& p) {
    _nvs.begin("smartclock", /*readOnly=*/true);
    _nvs.getString("ssid",    p.wifi_ssid, sizeof(p.wifi_ssid));
    _nvs.getString("pass",    p.wifi_pass, sizeof(p.wifi_pass));
    _nvs.getString("zip",     p.zip_code,  sizeof(p.zip_code));
    _nvs.getString("city",    p.city_name, sizeof(p.city_name));
    p.utc_offset_sec   = _nvs.getInt("utcofs",    -18000); // default US Eastern
    p.night_brightness = _nvs.getUChar("night_br", 50);   // default 50%

    // Stocks: load each symbol/name; fall back to compile-time defaults if not set.
    for (int i = 0; i < STOCK_COUNT; i++) {
        char sym_key[12], nam_key[12];
        snprintf(sym_key, sizeof(sym_key), "stk%d_sym", i);
        snprintf(nam_key, sizeof(nam_key), "stk%d_nam", i);
        _nvs.getString(sym_key, p.stock_symbols[i], sizeof(p.stock_symbols[i]));
        _nvs.getString(nam_key, p.stock_names[i],   sizeof(p.stock_names[i]));
        if (strlen(p.stock_symbols[i]) == 0) {
            strncpy(p.stock_symbols[i], STOCK_SYMBOLS_DEFAULT[i], sizeof(p.stock_symbols[i]) - 1);
            strncpy(p.stock_names[i],   STOCK_NAMES_DEFAULT[i],   sizeof(p.stock_names[i])   - 1);
        }
    }

    // NBA teams: default to LA Lakers (14) and Golden State Warriors (10).
    p.nba_team1_id = _nvs.getInt("nba_t1", 14);
    p.nba_team2_id = _nvs.getInt("nba_t2", 10);

    _nvs.end();
    return strlen(p.wifi_ssid) > 0;
}

// Save all preferences to NVS.
inline void prefs_save(const AppPrefs& p) {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putString("ssid",   p.wifi_ssid);
    _nvs.putString("pass",   p.wifi_pass);
    _nvs.putString("zip",    p.zip_code);
    _nvs.putString("city",   p.city_name);
    _nvs.putInt("utcofs",     p.utc_offset_sec);
    _nvs.putUChar("night_br", p.night_brightness);

    for (int i = 0; i < STOCK_COUNT; i++) {
        char sym_key[12], nam_key[12];
        snprintf(sym_key, sizeof(sym_key), "stk%d_sym", i);
        snprintf(nam_key, sizeof(nam_key), "stk%d_nam", i);
        _nvs.putString(sym_key, p.stock_symbols[i]);
        _nvs.putString(nam_key, p.stock_names[i]);
    }

    _nvs.putInt("nba_t1", p.nba_team1_id);
    _nvs.putInt("nba_t2", p.nba_team2_id);
    _nvs.end();
}

// Save only the UTC offset (called after every weather fetch).
inline void prefs_save_utc(int utc_offset_sec) {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putInt("utcofs", utc_offset_sec);
    _nvs.end();
}

// Save city name (called after successful geocode).
inline void prefs_save_city(const char* city) {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putString("city", city);
    _nvs.end();
}

// Save only night brightness (called by the setup-screen slider callback).
inline void prefs_save_brightness(uint8_t night_pct) {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putUChar("night_br", night_pct);
    _nvs.end();
}

// Save one stock entry (called on setup-screen textarea defocus).
inline void prefs_save_stock(int idx, const char* symbol, const char* name) {
    if (idx < 0 || idx >= STOCK_COUNT) return;
    char sym_key[12], nam_key[12];
    snprintf(sym_key, sizeof(sym_key), "stk%d_sym", idx);
    snprintf(nam_key, sizeof(nam_key), "stk%d_nam", idx);
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putString(sym_key, symbol);
    _nvs.putString(nam_key, name);
    _nvs.end();
}

// Save NBA team IDs (called on setup-screen dropdown change).
inline void prefs_save_nba_teams(int team1_id, int team2_id) {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.putInt("nba_t1", team1_id);
    _nvs.putInt("nba_t2", team2_id);
    _nvs.end();
}

// Clear all preferences (factory reset).
inline void prefs_clear() {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.clear();
    _nvs.end();
}
