#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// joke_api.h — Random joke from API League
//
// Endpoint: GET https://api.apileague.com/retrieve-random-joke
//             ?include-tags=kids&min-rating=0.5&max-length=250&api-key=KEY
//
// Response: { "id": N, "joke": "...", "setup": "...", "punchline": "...",
//             "rating": 4.5, "tags": [...] }
//
// Two-part jokes (knock-knock, Q&A) carry both "setup" and "punchline".
// One-liners carry only "joke" with "setup"/"punchline" absent or null.
//
// API League free tier: 50 tokens/day.  At a 3-hour interval the clock
// uses ≤ 8 requests/day, well within the limit.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

struct JokeData {
    char   setup[400];      // setup question or full one-liner text
    char   punchline[400];  // punchline; empty string for one-liners
    bool   valid;
    time_t fetch_time;      // epoch of last successful fetch (0 = never)
};

// ─── Fetch one joke from API League ──────────────────────────────────────────
static bool fetchJoke(JokeData& jd) {
    jd.valid = false;

    WiFiClientSecure client;
    client.setInsecure();

    char url[256];
    snprintf(url, sizeof(url), "%s?include-tags=%s&min-rating=0.5&max-length=250&api-key=%s",
             APILEAGUE_JOKE_BASE, APILEAGUE_JOKE_TAGS, APILEAGUE_API_KEY);

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("Accept", "application/json");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[JOKE] HTTP %d\n", code);
        http.end();
        client.stop();
        return false;
    }

    // Buffer full body — WiFiClientSecure streaming can cut off early.
    String body = http.getString();
    http.end();
    client.stop();

    static StaticJsonDocument<1024> doc;
    doc.clear();
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[JOKE] JSON: %s\n", err.c_str());
        return false;
    }

    // Two-part jokes use "setup" + "punchline"; one-liners use "joke".
    const char* setup     = doc["setup"]     | "";
    const char* punchline = doc["punchline"] | "";
    const char* joke      = doc["joke"]      | "";

    if (strlen(setup) > 0) {
        strncpy(jd.setup,     setup,     sizeof(jd.setup)     - 1);
        strncpy(jd.punchline, punchline, sizeof(jd.punchline) - 1);
    } else if (strlen(joke) > 0) {
        strncpy(jd.setup,     joke, sizeof(jd.setup) - 1);
        jd.punchline[0] = '\0';
    } else {
        Serial.println("[JOKE] Empty response");
        return false;
    }

    jd.setup    [sizeof(jd.setup)     - 1] = '\0';
    jd.punchline[sizeof(jd.punchline) - 1] = '\0';
    jd.valid      = true;
    jd.fetch_time = time(nullptr);

    if (strlen(jd.punchline) > 0) {
        Serial.printf("[JOKE] Q: %s\n[JOKE] A: %s\n", jd.setup, jd.punchline);
    } else {
        Serial.printf("[JOKE] %s\n", jd.setup);
    }
    return true;
}
