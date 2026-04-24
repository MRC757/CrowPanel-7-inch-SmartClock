#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// stock_api.h — Market data via Yahoo Finance chart API (per-symbol)
//
// No API key required. Uses the v8/finance/chart/{symbol} endpoint with HTTPS.
//
// SSL fix: _stocks_client is a file-scope static that persists across
// fetchStocks() calls. This means mbedtls_ssl_session_reset() is used on
// reconnect instead of mbedtls_ssl_setup(), avoiding the alloc/free cycle
// that fragments SRAM over hundreds of handshakes.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

struct StockInfo {
    char  symbol[12];        // e.g. "^GSPC"
    char  display_name[24];  // e.g. "S&P 500"
    float price;
    float change;            // Absolute change from previous close
    float change_pct;        // Percentage change
    bool  valid;
};

struct StocksData {
    StockInfo stocks[STOCK_COUNT];
    bool      valid;
    time_t    fetch_time = 0;
};

// Persistent SSL client — allocated once, never destroyed.
// Avoids mbedtls_ssl_setup() on every 5-minute fetch; uses session_reset instead.
static WiFiClientSecure _stocks_client;
static bool             _stocks_client_ready = false;

// ─── Fetch one symbol via the chart API ───────────────────────────────────────
static bool fetchStockChart(WiFiClientSecure& client, const char* symbol, StockInfo& si) {
    char url[128];
    {
        char encoded[24] = {};
        const char* base = YAHOO_CHART_BASE;
        int ui = 0;
        for (int i = 0; base[i] && ui < (int)sizeof(url) - 1; i++)
            url[ui++] = base[i];
        for (int i = 0; symbol[i] && ui < (int)sizeof(url) - 5; i++) {
            if      (symbol[i] == '^') { url[ui++]='%'; url[ui++]='5'; url[ui++]='E'; }
            else if (symbol[i] == '=') { url[ui++]='%'; url[ui++]='3'; url[ui++]='D'; }
            else                       { url[ui++] = symbol[i]; }
        }
        const char* qs = "?range=1d&interval=1d";
        for (int i = 0; qs[i] && ui < (int)sizeof(url) - 1; i++)
            url[ui++] = qs[i];
        url[ui] = '\0';
    }

    StaticJsonDocument<192> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"]              = true;

    static StaticJsonDocument<512> doc;

    bool parsed = false;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0) {
            Serial.printf("[STOCKS] %s retrying after 500 ms...\n", symbol);
            client.stop();   // force fresh TLS handshake on retry
            delay(500);
        }

        HTTPClient http;
        http.begin(client, url);
        http.setTimeout(10000);
        http.addHeader("User-Agent", "Mozilla/5.0 (compatible; SmartClock/1.0)");
        http.addHeader("Accept",     "application/json");
        int code = http.GET();

        if (code != 200) {
            Serial.printf("[STOCKS] %s HTTP %d (attempt %d)\n", symbol, code, attempt + 1);
            http.end();
            continue;
        }

        doc.clear();
        DeserializationError err = deserializeJson(doc, http.getStream(),
                                                   DeserializationOption::Filter(filter));
        http.end();

        if (err) {
            Serial.printf("[STOCKS] %s JSON: %s\n", symbol, err.c_str());
            continue;
        }
        parsed = true;
        break;
    }

    if (!parsed) return false;

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[STOCKS] %s: no meta in response\n", symbol);
        return false;
    }

    si.price = meta["regularMarketPrice"] | 0.0f;

    float prev = meta["regularMarketPreviousClose"] | 0.0f;
    if (prev == 0.0f) prev = meta["chartPreviousClose"] | 0.0f;
    if (prev == 0.0f) prev = meta["previousClose"]      | 0.0f;

    if (si.price > 0.0f && prev > 0.0f) {
        si.change     = si.price - prev;
        si.change_pct = (si.change / prev) * 100.0f;
    }

    Serial.printf("[STOCKS] %s price=%.2f\n", symbol, si.price);
    si.valid = (si.price > 0.0f);
    return si.valid;
}

// ─── Public fetch — one persistent TLS session for all symbols ────────────────
// symbols / names come from g_prefs (configurable via setup screen).
static bool fetchStocks(StocksData& sd,
                        const char symbols[][12],
                        const char names[][24]) {
    if (!_stocks_client_ready) {
        _stocks_client.setInsecure();
        _stocks_client_ready = true;
    }

    for (int i = 0; i < STOCK_COUNT; i++) {
        strncpy(sd.stocks[i].symbol,       symbols[i], sizeof(sd.stocks[i].symbol)       - 1);
        strncpy(sd.stocks[i].display_name, names[i],   sizeof(sd.stocks[i].display_name) - 1);
        sd.stocks[i].symbol[sizeof(sd.stocks[i].symbol) - 1]             = '\0';
        sd.stocks[i].display_name[sizeof(sd.stocks[i].display_name) - 1] = '\0';
    }

    bool anyNew = false;
    for (int i = 0; i < STOCK_COUNT; i++) {
        StockInfo tmp = sd.stocks[i];
        tmp.valid = false;
        if (fetchStockChart(_stocks_client, symbols[i], tmp)) {
            sd.stocks[i] = tmp;
            anyNew = true;
            Serial.printf("[STOCKS] %-10s $%10.2f  %+.2f (%+.2f%%)\n",
                          sd.stocks[i].display_name,
                          sd.stocks[i].price,
                          sd.stocks[i].change,
                          sd.stocks[i].change_pct);
        }
    }

    if (anyNew) {
        // Keep _stocks_client alive — next call uses mbedtls_ssl_session_reset()
        // (no context reallocation) instead of mbedtls_ssl_setup() (full alloc).
        sd.fetch_time = time(nullptr);
        sd.valid = true;
    } else {
        // Total failure: stop client to reset cleanly before next attempt.
        _stocks_client.stop();
    }

    return anyNew;
}
