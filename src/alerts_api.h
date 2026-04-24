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
    char event[48];      // e.g. "Tornado Warning", "Flash Flood Watch"
    char headline[128];  // short headline text
    char severity[12];   // "Extreme", "Severe", "Moderate", "Minor"
};

struct AlertsData {
    AlertInfo alerts[ALERTS_MAX];
    int       count;     // number of active alerts (0 = all clear)
    bool      valid;     // true after at least one successful fetch
};

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
    filter["features"][0]["properties"]["event"]    = true;
    filter["features"][0]["properties"]["headline"] = true;
    filter["features"][0]["properties"]["severity"] = true;

    static StaticJsonDocument<4096> doc; doc.clear();
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

            strncpy(ad.alerts[ad.count].event,    props["event"]    | "Unknown", 47);
            strncpy(ad.alerts[ad.count].headline, props["headline"] | "",        127);
            strncpy(ad.alerts[ad.count].severity, props["severity"] | "Unknown", 11);

            ad.alerts[ad.count].event[47]    = '\0';
            ad.alerts[ad.count].headline[127] = '\0';
            ad.alerts[ad.count].severity[11]  = '\0';
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
