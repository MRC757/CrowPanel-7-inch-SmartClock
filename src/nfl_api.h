#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// nfl_api.h — NFL games for the next 7 days via Ball Don't Lie API
//
// Endpoint: GET https://api.balldontlie.io/nfl/v1/games
// Auth:     Authorization: 3eac5689-5f83-4beb-944b-3b556c079a73   (no "Bearer" prefix)
// Filter:   dates[]=YYYY-MM-DD     (repeat for each of the 7 days)
// Response: data[] array; each game has:
//   date                     ISO-8601 UTC datetime "2024-09-06T00:20:00.000Z"
//   status                   "Scheduled", "Final", "F/OT", quarter strings, etc.
//   visitor_team.abbreviation  "BAL", "KC", ...
//   home_team.abbreviation     same
//   visitor_team_score / home_team_score  (null when not yet started)
//
// Requires: include/secrets.h with  #define BALLDONTLIE_API_KEY ""
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "config.h"
#include "secrets.h"   // BALLDONTLIE_API_KEY

#define NFL_MAX_GAMES 32

struct NflGame {
    char visitor_abbr[8];   // "BAL", "KC", "WAS", etc.
    char home_abbr[8];
    char day_str[5];        // "Sun", "Mon", "Thu", "Sat"
    char date_str[9];       // "Jan 18"
    char time_str[16];      // "7:00 PM", "28-14 Final", "14-10 Live"
    bool is_final;
    bool has_score;
};

struct NflData {
    NflGame games[NFL_MAX_GAMES];
    int     count;
    bool    valid;
};

// ─── Convert UTC ISO-8601 string to local time_t epoch ────────────────────────
// Formula: timegm(utc_tm) = mktime(utc_tm_as_if_local) + utc_offset_sec
// This works because mktime() treats the tm as local time; adding the offset
// corrects for the timezone so we get the true UTC epoch, which localtime()
// can then convert to the correct local broken-down time.
static time_t _nfl_iso_to_epoch(const char* iso, int utc_offset_sec) {
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

static bool fetchNfl(NflData& nd, int utc_offset_sec) {
    nd.count = 0;
    nd.valid = false;

    if (strlen(BALLDONTLIE_API_KEY) == 0) {
        Serial.println("[NFL] No API key — set BALLDONTLIE_API_KEY in include/secrets.h");
        nd.valid = true;   // mark valid so UI shows the "no key" message
        return true;
    }

    // ── Build 7-day date list from today's local date ──────────────────────
    struct tm today;
    if (!getLocalTime(&today, 500)) {
        Serial.println("[NFL] Cannot get local time");
        return false;
    }

    String url = String(BALLDONTLIE_NFL_BASE) + "?per_page=100";
    struct tm day_tm = today;
    for (int d = 0; d < 7; d++) {
        if (d > 0) {
            day_tm.tm_mday++;
            mktime(&day_tm);   // normalize (handles month/year wrap)
        }
        char date_buf[12];
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &day_tm);
        url += "&dates[]=";
        url += date_buf;
    }

    // ── HTTP request ───────────────────────────────────────────────────────
    Serial.printf("[NFL] URL: %s\n", url.c_str());

    WiFiClientSecure client;
    client.setInsecure();   // public API; no cert pinning needed

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(15000);
    http.addHeader("Authorization", BALLDONTLIE_API_KEY);

    int code = http.GET();
    Serial.printf("[NFL] HTTP %d  size=%d\n", code, http.getSize());
    if (code != 200) {
        http.end();
        client.stop();
        return false;
    }

    String body = http.getString();
    http.end();
    client.stop();

    // ── Parse JSON (filtered to keep only needed fields) ───────────────────
    // 512 bytes — sized generously; undersizing silently corrupts the filter
    // and causes ArduinoJson to discard all data.
    StaticJsonDocument<512> filter;
    filter["data"][0]["date"]                         = true;
    filter["data"][0]["status"]                       = true;
    filter["data"][0]["visitor_team"]["abbreviation"] = true;
    filter["data"][0]["home_team"]["abbreviation"]    = true;
    filter["data"][0]["visitor_team_score"]           = true;
    filter["data"][0]["home_team_score"]              = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    static StaticJsonDocument<2048> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[NFL] JSON err: %s\n", err.c_str());
        return false;
    }
    Serial.printf("[NFL] JSON ok, doc.mem=%d\n", (int)doc.memoryUsage());

    JsonArray data = doc["data"];
    if (data.isNull()) {
        Serial.println("[NFL] 'data' key missing from response");
        nd.valid = true;   // no games; valid empty result
        return true;
    }
    Serial.printf("[NFL] data.size()=%d\n", (int)data.size());

    for (JsonObject game : data) {
        if (nd.count >= NFL_MAX_GAMES) break;
        NflGame& g = nd.games[nd.count];

        // ── Team abbreviations ─────────────────────────────────────────────
        strncpy(g.visitor_abbr, game["visitor_team"]["abbreviation"] | "???", 7);
        strncpy(g.home_abbr,    game["home_team"]["abbreviation"]    | "???", 7);
        g.visitor_abbr[7] = '\0';
        g.home_abbr[7]    = '\0';

        // ── Parse UTC datetime → local day/date/time strings ──────────────
        const char* iso   = game["date"] | "";
        time_t      epoch = _nfl_iso_to_epoch(iso, utc_offset_sec);
        if (epoch > 0) {
            struct tm* lt = localtime(&epoch);
            strftime(g.day_str,  sizeof(g.day_str),  "%a",    lt);  // "Sun"
            strftime(g.date_str, sizeof(g.date_str), "%b %d", lt);  // "Jan 18"
            // Kickoff time — strip leading zero from hour
            char tbuf[12];
            strftime(tbuf, sizeof(tbuf), "%I:%M %p", lt);
            const char* tp = (tbuf[0] == '0') ? tbuf + 1 : tbuf;
            strncpy(g.time_str, tp, sizeof(g.time_str) - 1);
            g.time_str[sizeof(g.time_str) - 1] = '\0';
        } else {
            strncpy(g.day_str,  "???", sizeof(g.day_str));
            strncpy(g.date_str, "---", sizeof(g.date_str));
            strncpy(g.time_str, "TBD", sizeof(g.time_str));
        }

        // ── Status and score ───────────────────────────────────────────────
        const char* status = game["status"] | "";
        // "Final", "F/OT", "F/2OT", etc.
        g.is_final = (strstr(status, "Final") != nullptr ||
                      strstr(status, "final") != nullptr ||
                      (status[0] == 'F' && status[1] == '/'));

        int vs = game["visitor_team_score"] | -1;
        int hs = game["home_team_score"]    | -1;
        g.has_score = (vs >= 0 && hs >= 0);

        if (g.has_score) {
            if (g.is_final) {
                snprintf(g.time_str, sizeof(g.time_str), "%d-%d Final", vs, hs);
            } else {
                // In-progress: show live score
                snprintf(g.time_str, sizeof(g.time_str), "%d-%d", vs, hs);
            }
        }

        nd.count++;
    }

    nd.valid = true;
    Serial.printf("[NFL] %d game(s) in next 7 days\n", nd.count);
    return true;
}
