#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// alerts_api.h — Active weather alerts via NWS (api.weather.gov)
//
// Endpoint: https://api.weather.gov/alerts/active?point={lat},{lon}
// Free, no key, HTTPS, US only. Returns GeoJSON FeatureCollection.
// Uses lat/lon already stored in g_weather from the geocode step.
//
// The NWS response can be large; a tight filter keeps the parsed doc small.
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

static bool fetchAlerts(float lat, float lon, AlertsData& ad) {
    WiFiClientSecure client;
    client.setInsecure();   // NWS cert rotates; setInsecure is acceptable here

    HTTPClient http;
    String url = String("https://api.weather.gov/alerts/active?point=")
               + String(lat, 4) + "," + String(lon, 4);
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("User-Agent", "SmartClock/1.0 ESP32");
    http.addHeader("Accept",     "application/geo+json");

    int code = http.GET();
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

    // Filter: keep only event, headline, severity from each feature.
    // ArduinoJson 6: [0] on an array in a filter applies to ALL elements.
    StaticJsonDocument<256> filter;
    filter["features"][0]["properties"]["event"]    = true;
    filter["features"][0]["properties"]["headline"] = true;
    filter["features"][0]["properties"]["severity"] = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    static StaticJsonDocument<4096> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();

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

            // Null-terminate in case of truncation
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
