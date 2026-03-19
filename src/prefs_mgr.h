#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// prefs_mgr.h  — Persistent NVS storage via Arduino Preferences library
// Stores: WiFi SSID, WiFi password, ZIP code, city name, UTC offset,
//         night brightness.
// ─────────────────────────────────────────────────────────────────────────────
#include <Preferences.h>
#include <string.h>

struct AppPrefs {
    char    wifi_ssid[64];
    char    wifi_pass[64];
    char    zip_code[8];
    char    city_name[64];
    int     utc_offset_sec;
    uint8_t night_brightness;  // 10–100 %; applied from sunset to sunrise
};

static Preferences _nvs;

// Load all preferences from NVS. Returns true if WiFi credentials exist.
inline bool prefs_load(AppPrefs& p) {
    _nvs.begin("smartclock", /*readOnly=*/true);
    _nvs.getString("ssid",    p.wifi_ssid, sizeof(p.wifi_ssid));
    _nvs.getString("pass",    p.wifi_pass, sizeof(p.wifi_pass));
    _nvs.getString("zip",     p.zip_code,  sizeof(p.zip_code));
    _nvs.getString("city",    p.city_name, sizeof(p.city_name));
    p.utc_offset_sec  = _nvs.getInt("utcofs",    -18000); // default US Eastern
    p.night_brightness = _nvs.getUChar("night_br", 50);   // default 50%
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

// Clear all preferences (factory reset).
inline void prefs_clear() {
    _nvs.begin("smartclock", /*readOnly=*/false);
    _nvs.clear();
    _nvs.end();
}

