#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// joke_api.h — Random dad joke from RapidAPI Dad Jokes
//
// Endpoint: GET https://dad-jokes.p.rapidapi.com/random/joke
// Headers:  x-rapidapi-host: dad-jokes.p.rapidapi.com
//           x-rapidapi-key:  <RAPIDAPI_JOKE_KEY>
//
// Response: { "body": [{ "setup": "...", "punchline": "..." }], "success": true }
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

struct JokeData {
    char   setup[400];      // setup / question
    char   punchline[400];  // punchline
    bool   valid;
    time_t fetch_time;      // epoch of last successful fetch (0 = never)
};

// Replace multi-byte UTF-8 sequences that fall outside LVGL's compiled glyph
// range (Basic Latin + Latin-1, U+0020–U+00FF) with plain ASCII equivalents.
static void sanitizeForLvgl(char* s) {
    // Known 3-byte sequences (0xE2 0x80 xx) for common typographic characters
    static const struct { uint8_t b1, b2; const char* rep; } map3[] = {
        {0x80, 0x98, "'"},   // U+2018 left single quote
        {0x80, 0x99, "'"},   // U+2019 right single quote / apostrophe
        {0x80, 0x9C, "\""},  // U+201C left double quote
        {0x80, 0x9D, "\""},  // U+201D right double quote
        {0x80, 0x94, "-"},   // U+2014 em dash
        {0x80, 0x93, "-"},   // U+2013 en dash
        {0x80, 0xA6, "..."},  // U+2026 horizontal ellipsis
        {0x80, 0xA2, "*"},   // U+2022 bullet
    };

    uint8_t* p   = (uint8_t*)s;
    uint8_t* dst = (uint8_t*)s;
    while (*p) {
        if (*p == 0xE2 && *(p+1) && *(p+2)) {
            bool matched = false;
            for (auto& e : map3) {
                if (*(p+1) == e.b1 && *(p+2) == e.b2) {
                    const char* r = e.rep;
                    while (*r) *dst++ = (uint8_t)*r++;
                    p += 3;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                // Unknown 3-byte sequence — skip it
                p += 3;
            }
        } else if (*p >= 0x80) {
            // Any other non-ASCII byte: advance past the full sequence
            if      ((*p & 0xE0) == 0xC0) p += 2;  // 2-byte
            else if ((*p & 0xF0) == 0xE0) p += 3;  // 3-byte
            else if ((*p & 0xF8) == 0xF0) p += 4;  // 4-byte
            else                           p += 1;  // stray continuation
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';
}

// ─── Fetch one joke from Dad Jokes RapidAPI ──────────────────────────────────
static bool fetchJoke(JokeData& jd) {
    jd.valid = false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, DAD_JOKES_URL);
    http.setTimeout(10000);
    http.addHeader("Content-Type",   "application/json");
    http.addHeader("x-rapidapi-host", "dad-jokes.p.rapidapi.com");
    http.addHeader("x-rapidapi-key",  RAPIDAPI_JOKE_KEY);
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[JOKE] HTTP %d\n", code);
        http.end();
        client.stop();
        return false;
    }

    static StaticJsonDocument<1024> doc;
    doc.clear();
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    client.stop();
    if (err) {
        Serial.printf("[JOKE] JSON: %s\n", err.c_str());
        return false;
    }

    JsonArray arr = doc["body"].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
        Serial.println("[JOKE] Empty body array");
        return false;
    }

    const char* setup     = arr[0]["setup"]     | "";
    const char* punchline = arr[0]["punchline"] | "";

    if (strlen(setup) == 0) {
        Serial.println("[JOKE] Missing setup field");
        return false;
    }

    strncpy(jd.setup,     setup,     sizeof(jd.setup)     - 1);
    strncpy(jd.punchline, punchline, sizeof(jd.punchline) - 1);
    jd.setup    [sizeof(jd.setup)     - 1] = '\0';
    jd.punchline[sizeof(jd.punchline) - 1] = '\0';
    sanitizeForLvgl(jd.setup);
    sanitizeForLvgl(jd.punchline);
    jd.valid      = true;
    jd.fetch_time = time(nullptr);

    Serial.printf("[JOKE] Q: %s\n[JOKE] A: %s\n", jd.setup, jd.punchline);
    return true;
}
