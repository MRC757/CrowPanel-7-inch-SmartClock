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
    jd.valid      = true;
    jd.fetch_time = time(nullptr);

    Serial.printf("[JOKE] Q: %s\n[JOKE] A: %s\n", jd.setup, jd.punchline);
    return true;
}
