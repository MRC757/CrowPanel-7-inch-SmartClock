#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// iss_api.h — ISS visible pass predictions via N2YO API
//
// Free API key required (1 000 req/hr on free tier):
//   1. Register at https://www.n2yo.com/login/register.php
//   2. Copy your API key from My Account
//   3. Set N2YO_API_KEY in include/secrets.h
//
// Endpoint: N2YO_PASSES_BASE{lat}/{lon}/0/{days}/{minVisibility}&apiKey={key}
//   NORAD ID 25544 = ISS (ZARYA)
//   days = 3, minVisibility = 30 seconds (server-side minimum pass duration)
//   Passes with maxEl < 20° filtered client-side for horizon obstructions
//
// NOTE: The original Open Notify iss-pass.json endpoint is permanently
// decommissioned (~2023) and returns HTTP 404.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"
#include "json_buf.h"

#define ISS_MAX_PASSES 3

struct IssPass {
    time_t risetime;   // UTC Unix timestamp of pass start
    int    duration;   // visibility duration in seconds
};

struct IssData {
    IssPass passes[ISS_MAX_PASSES];
    int     count;
    bool    valid;
};

static bool fetchIss(float lat, float lon, IssData& id) {
    id = {};

    if (N2YO_API_KEY[0] == '\0') {
        Serial.println("[ISS] No N2YO API key — register free at n2yo.com");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    // /visualpasses/25544/{lat}/{lon}/{altM}/{days}/{minVisibility}/&apiKey={key}
    // N2YO requires the trailing slash before &apiKey= — it is part of the path.
    // minVisibility = minimum pass duration in seconds (not elevation degrees)
    // Passes below 20° peak elevation are filtered out after fetch.
    char url[192];
    snprintf(url, sizeof(url), "%s%.4f/%.4f/0/3/30/&apiKey=%s",
             N2YO_PASSES_BASE, lat, lon, N2YO_API_KEY);
    Serial.printf("[ISS] URL: %s\n", url);

    HTTPClient http;
    // N2YO responds with Transfer-Encoding: chunked on HTTP/1.1, and
    // http.getStream() does NOT decode chunking — ArduinoJson would parse the
    // hex chunk-size line as a bare number and silently yield an empty doc.
    // HTTP/1.0 forbids chunked encoding, forcing a plain body.
    http.useHTTP10(true);
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "SmartClock/1.0 ESP32");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[ISS] HTTP %d\n", code);
        http.end();
        client.stop();
        return false;
    }

    // Filter: startUTC, duration, and maxEl (peak elevation) from each pass.
    // 256 bytes — an undersized filter doc overflows SILENTLY and discards all
    // data (same failure mode documented in nfl_api.h); 96 was too small.
    StaticJsonDocument<256> filter;
    filter["passes"][0]["startUTC"] = true;
    filter["passes"][0]["duration"] = true;
    filter["passes"][0]["maxEl"]    = true;
    if (filter.overflowed()) Serial.println("[ISS] WARNING: filter doc overflowed");

    // Shared BSS parse buffer (json_buf.h) — internal SRAM, no heap/PSRAM.
    auto& doc = g_json_doc; doc.clear();
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    http.end();
    client.stop();
    if (err) {
        Serial.printf("[ISS] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonArray passes = doc["passes"];
    if (passes.isNull() || passes.size() == 0) {
        Serial.println("[ISS] No visible passes in next 3 days");
        id.valid = true;   // successful fetch, just no passes
        id.count = 0;
        return true;
    }

    id.count = 0;
    for (JsonObject pass : passes) {
        if (id.count >= ISS_MAX_PASSES) break;
        // N2YO sends maxEl as a float (e.g. 26.09). ArduinoJson's | operator
        // returns the default when types mismatch, so an int default reads 0.
        int maxEl = (int)(pass["maxEl"] | 0.0f);
        Serial.printf("[ISS] pass maxEl=%d duration=%ds\n", maxEl, (int)(pass["duration"] | 0));
        if (maxEl < 20) continue;   // skip passes blocked by horizon (trees, buildings)
        id.passes[id.count].risetime = pass["startUTC"] | (long)0;
        id.passes[id.count].duration = pass["duration"] | 0;
        id.count++;
    }

    // Mark fetch as successful regardless of elevation filter results.
    // count=0 here means "API returned passes but all were below 20°" — the UI
    // will display "No visible passes" which is correct and expected behavior.
    id.valid = true;
    Serial.printf("[ISS] Got %d visible passes (≥20° elevation)\n", id.count);
    return true;
}
