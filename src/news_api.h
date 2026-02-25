#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// news_api.h — Webz.io News API Lite: breaking headlines
//
// Endpoint : https://api.webz.io/newsApiLite
// Filters  : language=English, breaking=true
//            categories: economy, disaster, science_technology,
//                        war_conflict_unrest, weather
// Returns  : JSON  { "posts": [ { "title": "...", ... }, ... ] }
//
// ArduinoJson streaming deserialisation is used so the full response body
// is never buffered into a heap String (avoids a large PSRAM allocation).
// The JSON document is a static local (BSS = internal SRAM).
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

struct NewsData {
    char headlines[NEWS_MAX_HEADLINES][NEWS_HEADLINE_LEN];
    int  count;
    bool valid;
};

// ─── Build a scrolling ticker string: "Headline 1  |  Headline 2  |  ..." ──
static String buildTickerString(const NewsData& nd) {
    String ticker;
    for (int i = 0; i < nd.count; i++) {
        ticker += nd.headlines[i];
        if (i < nd.count - 1) ticker += "  |  ";
    }
    return ticker;
}

// ─── Fetch up to NEWS_MAX_HEADLINES breaking headlines from Webz.io ──────────
static bool fetchNews(NewsData& nd) {
    nd.count = 0;
    nd.valid = false;

    WiFiClientSecure client;
    client.setInsecure();   // public read-only endpoint; cert check not required
    HTTPClient http;
    http.begin(client, WEBZ_NEWS_URL);
    http.setTimeout(12000);
    http.addHeader("User-Agent", "SmartClock/1.0 ESP32");

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[NEWS] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Filter: pull only posts[*].title — the rest of each post is discarded
    // before ArduinoJson allocates any memory for it.
    StaticJsonDocument<64> filter;
    filter["posts"][0]["title"] = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    // doc.clear() resets the document state before each fetch.
    static StaticJsonDocument<4096> doc;
    doc.clear();

    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.printf("[NEWS] JSON err: %s\n", err.c_str());
        return false;
    }

    JsonArray posts = doc["posts"].as<JsonArray>();
    if (posts.isNull()) {
        Serial.println("[NEWS] No 'posts' array in response");
        return false;
    }

    for (JsonObject post : posts) {
        if (nd.count >= NEWS_MAX_HEADLINES) break;
        const char* title = post["title"] | "";
        if (strlen(title) > 0) {
            strlcpy(nd.headlines[nd.count], title, NEWS_HEADLINE_LEN);
            nd.count++;
        }
    }

    nd.valid = (nd.count > 0);
    Serial.printf("[NEWS] Got %d headlines\n", nd.count);
    return nd.valid;
}
