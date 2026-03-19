#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// iss_api.h — ISS visible pass predictions via N2YO API
//
// Free API key required (1 000 req/hr on free tier):
//   1. Register at https://www.n2yo.com/login/register.php
//   2. Copy your API key from My Account
//   3. Set N2YO_API_KEY in include/secrets.h
//
// Endpoint: N2YO_PASSES_BASE{lat}/{lon}/0/{days}/{minEl}/&apiKey={key}
//   NORAD ID 25544 = ISS (ZARYA)
//   days = 3, minElevation = 10°
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

    // /visualpasses/25544/{lat}/{lon}/{altM}/{days}/{minElev}/&apiKey={key}
    String url = String(N2YO_PASSES_BASE)
               + String(lat, 4) + "/" + String(lon, 4)
               + "/0/3/10/&apiKey=" + N2YO_API_KEY;

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "SmartClock/1.0 ESP32");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[ISS] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // Filter: only startUTC and duration from each pass
    StaticJsonDocument<96> filter;
    filter["passes"][0]["startUTC"] = true;
    filter["passes"][0]["duration"] = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    static StaticJsonDocument<512> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[ISS] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonArray passes = doc["passes"];
    if (passes.isNull() || passes.size() == 0) {
        Serial.println("[ISS] No visible passes in next 3 days");
        return false;
    }

    id.count = 0;
    for (JsonObject pass : passes) {
        if (id.count >= ISS_MAX_PASSES) break;
        id.passes[id.count].risetime = pass["startUTC"] | (long)0;
        id.passes[id.count].duration = pass["duration"] | 0;
        id.count++;
    }

    id.valid = (id.count > 0);
    Serial.printf("[ISS] Got %d upcoming visible passes\n", id.count);
    return id.valid;
}
