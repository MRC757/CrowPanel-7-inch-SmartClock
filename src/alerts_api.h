#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// alerts_api.h — Active weather alerts via NWS (api.weather.gov)
//
// Endpoint: https://api.weather.gov/alerts/active?point={lat},{lon}
// Free, no key, HTTPS, US only.
//
// SSL fix: _alerts_client is a file-scope static that persists across calls.
// On reconnect, mbedtls_ssl_session_reset() is used instead of
// mbedtls_ssl_setup(), avoiding the alloc/free cycle that fragments SRAM.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define ALERTS_MAX 3

struct AlertInfo {
    char event[48];           // e.g. "Tornado Warning", "Flash Flood Watch"
    char headline[128];       // short headline text
    char severity[12];        // "Extreme", "Severe", "Moderate", "Minor"
    char urgency[16];         // "Immediate", "Expected", "Future"
    char response[16];        // "Execute", "Prepare", "Monitor", "Assess"
    char description[512];    // full details: "* WHAT... * WHERE... * WHEN... * IMPACTS..."
    time_t onset;             // Unix timestamp when alert begins
    time_t expires;           // Unix timestamp when alert expires
};

struct AlertsData {
    AlertInfo alerts[ALERTS_MAX];
    int       count;     // number of active alerts (0 = all clear)
    bool      valid;     // true after at least one successful fetch
};

// ─── Helper: Parse ISO 8601 timestamp to Unix time ────────────────────────────
// Format: "2026-05-07T23:00:00-04:00" → Unix timestamp (ignoring timezone offset)
static time_t _parse_iso8601(const char* iso) {
    if (!iso || iso[0] == '\0') return 0;
    struct tm t = {};
    sscanf(iso, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
           &t.tm_hour, &t.tm_min, &t.tm_sec);
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return mktime(&t);
}

// Persistent SSL client — keeps TLS context alive between 5-minute fetches.
static WiFiClientSecure _alerts_client;
static bool             _alerts_client_ready = false;

static bool fetchAlerts(float lat, float lon, AlertsData& ad) {
    char url[96];
    snprintf(url, sizeof(url), "https://api.weather.gov/alerts/active?point=%.4f,%.4f", lat, lon);

    if (!_alerts_client_ready) {
        _alerts_client.setInsecure();
        _alerts_client_ready = true;
    }

    // Retry loop: on SSL failure (-1), stop client and retry with a fresh handshake.
    // First attempt reuses the existing TLS session (or reconnects via session_reset
    // if NWS has closed the connection) — no context reallocation either way.
    HTTPClient http;
    int code = 0;

    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            Serial.println("[ALERTS] retrying after 500 ms...");
            _alerts_client.stop();   // reset on retry — clean slate for fresh handshake
            delay(500);
        }
        http.begin(_alerts_client, url);
        http.setTimeout(15000);
        http.addHeader("User-Agent", "SmartClock/1.0 ESP32");
        http.addHeader("Accept",     "application/geo+json");
        code = http.GET();
        if (code == -1) {
            Serial.printf("[ALERTS] attempt %d HTTP %d\n", attempt + 1, code);
            http.end();
            continue;
        }
        break;
    }

    if (code != 200) {
        // 404 = point not supported (offshore / non-US). Treat as no alerts.
        if (code == 404) {
            ad.count = 0;
            ad.valid = true;
            http.end();
            return true;
        }
        Serial.printf("[ALERTS] HTTP %d\n", code);
        http.end();
        return false;
    }

    StaticJsonDocument<256> filter;
    filter["features"][0]["properties"]["event"]       = true;
    filter["features"][0]["properties"]["headline"]    = true;
    filter["features"][0]["properties"]["severity"]    = true;
    filter["features"][0]["properties"]["urgency"]     = true;
    filter["features"][0]["properties"]["response"]    = true;
    filter["features"][0]["properties"]["description"] = true;
    filter["features"][0]["properties"]["onset"]       = true;
    filter["features"][0]["properties"]["expires"]     = true;

    static StaticJsonDocument<2048> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    // Keep _alerts_client alive for next fetch — avoids mbedtls_ssl_setup() on reconnect.

    if (err) {
        Serial.printf("[ALERTS] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonArray features = doc["features"];
    ad.count = 0;

    if (!features.isNull()) {
        for (JsonObject feat : features) {
            if (ad.count >= ALERTS_MAX) break;
            JsonObject props = feat["properties"];
            if (props.isNull()) continue;

            strncpy(ad.alerts[ad.count].event,       props["event"]       | "Unknown", 47);
            strncpy(ad.alerts[ad.count].headline,    props["headline"]    | "",        127);
            strncpy(ad.alerts[ad.count].severity,    props["severity"]    | "Unknown", 11);
            strncpy(ad.alerts[ad.count].urgency,     props["urgency"]     | "Unknown", 15);
            strncpy(ad.alerts[ad.count].response,    props["response"]    | "Monitor",  15);
            strncpy(ad.alerts[ad.count].description, props["description"] | "",        511);

            ad.alerts[ad.count].event[47]       = '\0';
            ad.alerts[ad.count].headline[127]   = '\0';
            ad.alerts[ad.count].severity[11]    = '\0';
            ad.alerts[ad.count].urgency[15]     = '\0';
            ad.alerts[ad.count].response[15]    = '\0';
            ad.alerts[ad.count].description[511] = '\0';

            // Parse ISO-8601 timestamps to Unix time
            const char* onset_str = props["onset"] | "";
            const char* expires_str = props["expires"] | "";
            ad.alerts[ad.count].onset = _parse_iso8601(onset_str);
            ad.alerts[ad.count].expires = _parse_iso8601(expires_str);

            ad.count++;
        }
    }

    ad.valid = true;
    if (ad.count > 0) {
        Serial.printf("[ALERTS] %d active alert(s): %s\n",
                      ad.count, ad.alerts[0].event);
    } else {
        Serial.println("[ALERTS] All clear — no active alerts");
    }
    return true;
}
