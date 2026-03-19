#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// weather_api.h — Two-step weather fetch (no API key required)
//
//  Step 1: ZIP → lat/lon/city  via  api.zippopotam.us   (HTTPS, free, no key)
//  Step 2: lat/lon → weather   via  api.open-meteo.com  (HTTPS, free, no key)
//
// Open-Meteo returns WMO weather codes, current temp, humidity, wind speed,
// feels-like temperature, and utc_offset_seconds (includes DST automatically).
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ─── 5-day daily forecast entry ───────────────────────────────────────────
#define FORECAST_DAYS  5
#define HOURLY_COUNT  72   // 3 days × 24 hours

struct DailyForecast {
    char  day_name[4];   // "Mon", "Tue", etc.
    int   weather_code;
    float temp_max_f;
    float temp_min_f;
    float precip_in;     // precipitation in inches
    float uv_index_max;  // daily UV index maximum
    char  sunrise_str[9]; // "H:MM AM\0" local time
    char  sunset_str[9];  // "H:MM PM\0" local time
};

struct HourlyForecast {
    float  temp_f    [HOURLY_COUNT];   // °F per hour
    float  wind_mph  [HOURLY_COUNT];   // mph per hour
    int8_t precip_pct[HOURLY_COUNT];   // precipitation probability 0-100 %
    int    count;                      // valid hours populated
};

struct WeatherData {
    float temp_f;          // Current temperature °F
    float feels_like_f;    // Apparent temperature °F
    int   humidity_pct;    // Relative humidity %
    float wind_mph;        // Wind speed mph
    int   weather_code;    // WMO weather interpretation code
    char  description[32]; // Human-readable condition string
    char  city_name[64];   // City, State from Zippopotam
    float latitude;
    float longitude;
    int   utc_offset_sec;  // Current UTC offset incl. DST (from Open-Meteo)
    bool  valid;
    DailyForecast  forecast[FORECAST_DAYS];
    int            forecast_count;   // valid daily entries
    HourlyForecast hourly;           // 72-hour hourly data
    // Today's sunrise/sunset as integer hours + minutes (local time, 24-hour).
    // Populated after fetchOpenMeteo() for use by auto-dim logic.
    int   sunrise_hour;    // 0–23
    int   sunrise_minute;  // 0–59
    int   sunset_hour;     // 0–23
    int   sunset_minute;   // 0–59
};

// ─── WMO code → short label (fits in a narrow forecast card) ─────────────
static const char* wmo_desc_short(int code) {
    if (code == 0)              return "Clear";
    if (code <= 3)              return "Pt Cloudy";
    if (code <= 48)             return "Foggy";
    if (code <= 55)             return "Drizzle";
    if (code <= 65)             return "Rainy";
    if (code <= 77)             return "Snow";
    if (code <= 82)             return "Showers";
    if (code <= 86)             return "Snow Shwrs";
    if (code == 95)             return "T-Storm";
    if (code <= 99)             return "Svr T-Storm";
    return "Unknown";
}

// ─── WMO code → human-readable description ────────────────────────────────
static const char* wmo_description(int code) {
    if (code == 0)              return "Clear Sky";
    if (code <= 3)              return "Partly Cloudy";
    if (code <= 48)             return "Foggy";
    if (code <= 55)             return "Drizzle";
    if (code <= 65)             return "Rainy";
    if (code <= 77)             return "Snowy";
    if (code <= 82)             return "Showers";
    if (code <= 86)             return "Snow Showers";
    if (code == 95)             return "Thunderstorm";
    if (code <= 99)             return "Severe Thunderstorm";
    return "Unknown";
}

// ─── Helper: "YYYY-MM-DDTHH:MM" → "H:MM AM/PM" ───────────────────────────
static void _fmt_iso_time(const char* iso, char* buf, size_t sz) {
    const char* tptr = iso ? strchr(iso, 'T') : nullptr;
    if (tptr) {
        int hr = 0, mn = 0;
        sscanf(tptr + 1, "%d:%d", &hr, &mn);
        int hr12 = hr % 12;
        if (hr12 == 0) hr12 = 12;
        snprintf(buf, sz, "%d:%02d %s", hr12, mn, (hr >= 12) ? "PM" : "AM");
    } else {
        strncpy(buf, "--:--", sz - 1);
        buf[sz - 1] = '\0';
    }
}

// ─── Step 1: ZIP code → lat, lon, city name ───────────────────────────────
// Returns true on success. On failure, leaves WeatherData unchanged.
static bool fetchGeocode(const char* zip, WeatherData& wd) {
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification (public data, no risk)

    HTTPClient http;
    String url = String(ZIPPOPOTAM_BASE) + zip;
    http.begin(client, url);
    http.setTimeout(8000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[GEO] HTTP %d for zip %s\n", code, zip);
        http.end();
        return false;
    }

    // Response is ~200 bytes; a small document is fine.
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();

    if (err) {
        Serial.printf("[GEO] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonObject place = doc["places"][0];
    if (place.isNull()) {
        Serial.println("[GEO] No place found");
        return false;
    }

    wd.latitude  = place["latitude"].as<float>();
    wd.longitude = place["longitude"].as<float>();

    // Build "City, ST" display name
    const char* placeName  = place["place name"] | "Unknown";
    const char* stateAbbr  = place["state abbreviation"] | "";
    snprintf(wd.city_name, sizeof(wd.city_name), "%s, %s", placeName, stateAbbr);

    Serial.printf("[GEO] %s → %.4f, %.4f\n", wd.city_name, wd.latitude, wd.longitude);
    return true;
}

// ─── Step 2: lat/lon → weather ────────────────────────────────────────────
static bool fetchOpenMeteo(WeatherData& wd) {
    WiFiClientSecure client;
    client.setInsecure();

    // Request current weather + 5-day daily forecast.
    // timezone=auto lets Open-Meteo pick the right timezone from coordinates.
    // NOTE: current_weather= is deprecated; use current= with explicit field names.
    String url = String(OPEN_METEO_BASE)
        + "?latitude="  + String(wd.latitude,  4)
        + "&longitude=" + String(wd.longitude, 4)
        + "&current=temperature_2m,relative_humidity_2m,apparent_temperature,wind_speed_10m,weather_code"
        + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,uv_index_max,sunrise,sunset"
        + "&hourly=temperature_2m,wind_speed_10m,precipitation_probability"
        + "&temperature_unit=fahrenheit"
        + "&wind_speed_unit=mph"
        + "&precipitation_unit=inch"
        + "&timezone=auto"
        + "&forecast_days=5"
        + "&forecast_hours=72";

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[WX] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Read body into String first — more reliable than getStream() + filter
    // for WiFiClientSecure where the TLS layer may deliver data in chunks.
    String body = http.getString();
    http.end();

    if (body.length() < 50) {
        Serial.printf("[WX] Response too short (%u bytes)\n", body.length());
        return false;
    }

    // Filter: only the fields we actually display.
    StaticJsonDocument<1024> filter;
    filter["current"]["temperature_2m"]             = true;
    filter["current"]["relative_humidity_2m"]       = true;
    filter["current"]["apparent_temperature"]       = true;
    filter["current"]["wind_speed_10m"]             = true;
    filter["current"]["weather_code"]               = true;
    filter["utc_offset_seconds"]                    = true;
    filter["daily"]["time"]                         = true;
    filter["daily"]["weather_code"]                 = true;
    filter["daily"]["temperature_2m_max"]           = true;
    filter["daily"]["temperature_2m_min"]           = true;
    filter["daily"]["precipitation_sum"]            = true;
    filter["daily"]["uv_index_max"]                 = true;
    filter["daily"]["sunrise"]                      = true;
    filter["daily"]["sunset"]                       = true;
    filter["hourly"]["temperature_2m"]              = true;
    filter["hourly"]["wind_speed_10m"]              = true;
    filter["hourly"]["precipitation_probability"]   = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    // doc.clear() resets the document state before each use.
    static StaticJsonDocument<8192> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[WX] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    wd.temp_f        = cur["temperature_2m"]        | 0.0f;
    wd.wind_mph      = cur["wind_speed_10m"]         | 0.0f;
    wd.weather_code  = cur["weather_code"]           | 0;
    wd.humidity_pct  = cur["relative_humidity_2m"]   | 0;
    wd.feels_like_f  = cur["apparent_temperature"]   | wd.temp_f;

    wd.utc_offset_sec = doc["utc_offset_seconds"] | -18000;

    strncpy(wd.description, wmo_description(wd.weather_code), sizeof(wd.description) - 1);
    wd.valid = true;

    // ── Parse 5-day daily forecast ────────────────────────────────────────
    JsonArray dates   = doc["daily"]["time"];
    JsonArray codes   = doc["daily"]["weather_code"];
    JsonArray maxT    = doc["daily"]["temperature_2m_max"];
    JsonArray minT    = doc["daily"]["temperature_2m_min"];
    JsonArray precip  = doc["daily"]["precipitation_sum"];
    JsonArray uvArr   = doc["daily"]["uv_index_max"];
    JsonArray srArr   = doc["daily"]["sunrise"];
    JsonArray ssArr   = doc["daily"]["sunset"];

    wd.forecast_count = (int)min((size_t)FORECAST_DAYS, dates.size());
    for (int i = 0; i < wd.forecast_count; i++) {
        const char* ds = dates[i] | "";
        struct tm t = {};
        sscanf(ds, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
        t.tm_year -= 1900;
        t.tm_mon  -= 1;
        mktime(&t);
        strftime(wd.forecast[i].day_name, sizeof(wd.forecast[i].day_name), "%a", &t);
        wd.forecast[i].weather_code  = codes[i]  | 0;
        wd.forecast[i].temp_max_f    = maxT[i]   | 0.0f;
        wd.forecast[i].temp_min_f    = minT[i]   | 0.0f;
        wd.forecast[i].precip_in     = precip[i] | 0.0f;
        wd.forecast[i].uv_index_max  = uvArr.size() > (size_t)i
                                        ? (float)uvArr[i].as<float>() : 0.0f;
        _fmt_iso_time(srArr.size() > (size_t)i ? (const char*)srArr[i] : nullptr,
                      wd.forecast[i].sunrise_str, sizeof(wd.forecast[i].sunrise_str));
        _fmt_iso_time(ssArr.size() > (size_t)i ? (const char*)ssArr[i] : nullptr,
                      wd.forecast[i].sunset_str,  sizeof(wd.forecast[i].sunset_str));
    }

    // ── Extract today's sunrise/sunset as integers for auto-dim logic ────────
    // Open-Meteo ISO format: "YYYY-MM-DDTHH:MM" (local time, already DST-aware)
    wd.sunrise_hour = wd.sunrise_minute = 6;   // safe defaults (6:00 / 20:00)
    wd.sunset_hour  = 20; wd.sunset_minute = 0;
    if (srArr.size() > 0) {
        const char* sr = srArr[0] | "";
        const char* t  = strchr(sr, 'T');
        if (t) sscanf(t + 1, "%d:%d", &wd.sunrise_hour, &wd.sunrise_minute);
    }
    if (ssArr.size() > 0) {
        const char* ss = ssArr[0] | "";
        const char* t  = strchr(ss, 'T');
        if (t) sscanf(t + 1, "%d:%d", &wd.sunset_hour, &wd.sunset_minute);
    }

    // ── Parse 72-hour hourly forecast ─────────────────────────────────────
    JsonArray hTemp  = doc["hourly"]["temperature_2m"];
    JsonArray hWind  = doc["hourly"]["wind_speed_10m"];
    JsonArray hPrecp = doc["hourly"]["precipitation_probability"];

    wd.hourly.count = (int)min((size_t)HOURLY_COUNT, hTemp.size());
    for (int i = 0; i < wd.hourly.count; i++) {
        wd.hourly.temp_f[i]   = hTemp[i]  | 0.0f;
        wd.hourly.wind_mph[i] = hWind[i]  | 0.0f;
        int p = (hPrecp.size() > (size_t)i) ? (int)(hPrecp[i] | 0) : 0;
        wd.hourly.precip_pct[i] = (int8_t)(p < 0 ? 0 : p > 100 ? 100 : p);
    }

    Serial.printf("[WX] %.1f°F %s | Humidity %d%% | Wind %.1f mph | UTC%+d | %d-day fcst | %d hourly pts\n",
                  wd.temp_f, wd.description, wd.humidity_pct, wd.wind_mph,
                  wd.utc_offset_sec / 3600, wd.forecast_count, wd.hourly.count);
    return true;
}

// ─── Public entry point ────────────────────────────────────────────────────
// Fetches both geocode (if zip changed) and weather.
// Pass forceGeocode=true on first run or when zip changes.
static bool fetchWeather(const char* zip, WeatherData& wd, bool forceGeocode = false) {
    if (forceGeocode || wd.latitude == 0.0f) {
        if (!fetchGeocode(zip, wd)) return false;
    }
    return fetchOpenMeteo(wd);
}
