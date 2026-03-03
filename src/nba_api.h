#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// nba_api.h — LA Lakers + Golden State Warriors games for the next 7 days
//             via Ball Don't Lie API v1
//
// Endpoint: GET https://api.balldontlie.io/v1/games
// Auth:     Authorization: <BALLDONTLIE_API_KEY>  (same key as NFL)
// Filter:   team_ids[]=14  (LA Lakers)
//           team_ids[]=10  (Golden State Warriors)
//           start_date=YYYY-MM-DD  (today)
//           end_date=YYYY-MM-DD    (today + 6 days)
//
// Response fields used:
//   datetime                   ISO-8601 UTC "2025-01-06T03:00:00.000Z"
//   date                       "YYYY-MM-DD" fallback for scheduled games
//   visitor_team.abbreviation  "LAL", "GSW", ...
//   home_team.abbreviation     same
//
// Requires: include/secrets.h  →  #define BALLDONTLIE_API_KEY "..."
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "secrets.h"   // BALLDONTLIE_API_KEY

#define NBA_MAX_GAMES 20

struct NbaGame {
    char visitor_abbr[8];   // "LAL", "GSW", etc.
    char home_abbr[8];
    char day_str[5];        // "Mon", "Tue", ...
    char date_str[9];       // "Jan 18"
    char time_str[12];      // "7:30 PM" or "TBD"
};

struct NbaData {
    NbaGame games[NBA_MAX_GAMES];
    int     count;
    bool    valid;
};

// ─── Convert UTC ISO-8601 datetime to local time_t ────────────────────────────
static time_t _nba_iso_to_epoch(const char* iso, int utc_offset_sec) {
    int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
    if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &s) < 6) return 0;
    struct tm t = {};
    t.tm_year  = Y - 1900;
    t.tm_mon   = M - 1;
    t.tm_mday  = D;
    t.tm_hour  = h;
    t.tm_min   = m;
    t.tm_sec   = s;
    t.tm_isdst = -1;
    return mktime(&t) + (time_t)utc_offset_sec;
}

static bool fetchNba(NbaData& nd, int utc_offset_sec) {
    nd.count = 0;
    nd.valid = false;

    if (strlen(BALLDONTLIE_API_KEY) == 0) {
        Serial.println("[NBA] No API key — set BALLDONTLIE_API_KEY in include/secrets.h");
        nd.valid = true;   // mark valid so UI shows the "no key" message
        return true;
    }

    // ── Build start_date (today) and end_date (today + 6 days) ────────────
    struct tm today;
    if (!getLocalTime(&today, 500)) {
        Serial.println("[NBA] Cannot get local time");
        return false;
    }

    char start_buf[12], end_buf[12];
    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d", &today);

    struct tm end_tm = today;
    end_tm.tm_mday += 6;
    mktime(&end_tm);   // normalize (handles month/year wrap)
    strftime(end_buf, sizeof(end_buf), "%Y-%m-%d", &end_tm);

    // ── Build URL ─────────────────────────────────────────────────────────
    String url = String(BALLDONTLIE_NBA_BASE)
               + "?per_page=100"
               + "&team_ids[]=14&team_ids[]=10"
               + "&start_date=" + start_buf
               + "&end_date="   + end_buf;
    Serial.printf("[NBA] URL: %s\n", url.c_str());

    // ── HTTP request ───────────────────────────────────────────────────────
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Authorization", BALLDONTLIE_API_KEY);

    int code = http.GET();
    Serial.printf("[NBA] HTTP %d  size=%d\n", code, http.getSize());
    if (code != 200) {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
#if SMART_CLOCK_DEBUG
    Serial.printf("[NBA] body (first 300): %.300s\n", body.c_str());
#endif

    // ── Parse JSON (minimal filter: teams + dates only) ───────────────────
    StaticJsonDocument<256> filter;
    filter["data"][0]["datetime"]                     = true;
    filter["data"][0]["date"]                         = true;
    filter["data"][0]["visitor_team"]["abbreviation"] = true;
    filter["data"][0]["home_team"]["abbreviation"]    = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    static StaticJsonDocument<4096> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[NBA] JSON err: %s\n", err.c_str());
        return false;
    }
    Serial.printf("[NBA] JSON ok, doc.mem=%d\n", (int)doc.memoryUsage());

    JsonArray data = doc["data"];
    if (data.isNull()) {
        Serial.println("[NBA] 'data' key missing from response");
        nd.valid = true;
        return true;
    }
    Serial.printf("[NBA] data.size()=%d\n", (int)data.size());

    for (JsonObject game : data) {
        if (nd.count >= NBA_MAX_GAMES) break;
        NbaGame& g = nd.games[nd.count];

        // ── Team abbreviations ─────────────────────────────────────────────
        strncpy(g.visitor_abbr, game["visitor_team"]["abbreviation"] | "???", 7);
        strncpy(g.home_abbr,    game["home_team"]["abbreviation"]    | "???", 7);
        g.visitor_abbr[7] = '\0';
        g.home_abbr[7]    = '\0';

        // ── Parse tip-off time from datetime (or date as fallback) ─────────
        const char* iso  = game["datetime"] | "";
        const char* date = game["date"]     | "";
        time_t epoch = 0;

        if (iso[0] != '\0') {
            epoch = _nba_iso_to_epoch(iso, utc_offset_sec);
        }

        if (epoch > 0) {
            struct tm* lt = localtime(&epoch);
            strftime(g.day_str,  sizeof(g.day_str),  "%a",    lt);  // "Mon"
            strftime(g.date_str, sizeof(g.date_str), "%b %d", lt);  // "Jan 18"
            char tbuf[12];
            strftime(tbuf, sizeof(tbuf), "%I:%M %p", lt);
            const char* tp = (tbuf[0] == '0') ? tbuf + 1 : tbuf;
            strncpy(g.time_str, tp, sizeof(g.time_str) - 1);
            g.time_str[sizeof(g.time_str) - 1] = '\0';
        } else if (date[0] != '\0') {
            // Date-only field: "YYYY-MM-DD" for day/date, no time known
            int Y = 0, Mo = 0, D = 0;
            if (sscanf(date, "%4d-%2d-%2d", &Y, &Mo, &D) == 3) {
                struct tm t = {};
                t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D;
                t.tm_isdst = -1;
                epoch = mktime(&t) + (time_t)utc_offset_sec;
                struct tm* lt = localtime(&epoch);
                strftime(g.day_str,  sizeof(g.day_str),  "%a",    lt);
                strftime(g.date_str, sizeof(g.date_str), "%b %d", lt);
            } else {
                strncpy(g.day_str,  "???", sizeof(g.day_str));
                strncpy(g.date_str, "---", sizeof(g.date_str));
            }
            strncpy(g.time_str, "TBD", sizeof(g.time_str));
        } else {
            strncpy(g.day_str,  "???", sizeof(g.day_str));
            strncpy(g.date_str, "---", sizeof(g.date_str));
            strncpy(g.time_str, "TBD", sizeof(g.time_str));
        }

        nd.count++;
    }

    nd.valid = true;
    Serial.printf("[NBA] %d game(s) in next 7 days\n", nd.count);
    return true;
}
