#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// stock_api.h — Market data via Yahoo Finance chart API (per-symbol)
//
// No API key required. Uses the v8/finance/chart/{symbol} endpoint with HTTPS.
// The multi-symbol v8/finance/quote endpoint returns HTTP 500 from non-browser
// clients; the per-symbol chart endpoint is more permissive.
//
// Symbols fetched: ^GSPC (S&P 500), ^DJI (DOW), VYMI, VYM
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
};

// ─── Fetch one symbol via the chart API ───────────────────────────────────
static bool fetchStockChart(const char* symbol, StockInfo& si) {
    WiFiClientSecure client;
    client.setInsecure();

    // URL-encode special characters in ticker symbols:
    //   ^ → %5E  (index symbols: ^GSPC, ^DJI)
    //   = → %3D  (futures symbols: GC=F, SI=F)
    String sym = String(symbol);
    sym.replace("^", "%5E");
    sym.replace("=", "%3D");

    String url = String(YAHOO_CHART_BASE) + sym + "?range=1d&interval=1d";

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "Mozilla/5.0 (compatible; SmartClock/1.0)");
    http.addHeader("Accept",     "application/json");
    int code = http.GET();

    if (code != 200) {
        Serial.printf("[STOCKS] %s HTTP %d\n", symbol, code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    // Filter to just the meta fields we need.
    // chartPreviousClose is always present for both stocks and indices.
    // regularMarketPreviousClose is often absent for index symbols (^GSPC, ^DJI).
    StaticJsonDocument<192> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"]              = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
    static StaticJsonDocument<512> doc; doc.clear();
    DeserializationError err = deserializeJson(doc, body,
                                               DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[STOCKS] %s JSON: %s\n", symbol, err.c_str());
        return false;
    }

    JsonObject meta = doc["chart"]["result"][0]["meta"];
    if (meta.isNull()) {
        Serial.printf("[STOCKS] %s: no meta in response\n", symbol);
        return false;
    }

    si.price = meta["regularMarketPrice"] | 0.0f;

    // chartPreviousClose is the most reliable fallback — always present, and is
    // the value Yahoo itself uses as the chart baseline.  Index symbols (^GSPC,
    // ^DJI) often omit regularMarketPreviousClose entirely.
    float prev = meta["regularMarketPreviousClose"] | 0.0f;
    if (prev == 0.0f) prev = meta["chartPreviousClose"]         | 0.0f;
    if (prev == 0.0f) prev = meta["previousClose"]              | 0.0f;

    if (si.price > 0.0f && prev > 0.0f) {
        si.change     = si.price - prev;
        si.change_pct = (si.change / prev) * 100.0f;
    }

    Serial.printf("[STOCKS] %s price=%.2f\n", symbol, si.price);

    si.valid = (si.price > 0.0f);
    return si.valid;
}

// ─── Public fetch function — one HTTPS request per symbol ─────────────────
static bool fetchStocks(StocksData& sd) {
    for (int i = 0; i < STOCK_COUNT; i++) {
        strncpy(sd.stocks[i].symbol,       STOCK_SYMBOLS[i],       sizeof(sd.stocks[i].symbol) - 1);
        strncpy(sd.stocks[i].display_name, STOCK_DISPLAY_NAMES[i], sizeof(sd.stocks[i].display_name) - 1);
        sd.stocks[i].valid = false;
    }

    bool anyValid = false;
    for (int i = 0; i < STOCK_COUNT; i++) {
        if (fetchStockChart(STOCK_SYMBOLS[i], sd.stocks[i])) {
            anyValid = true;
            Serial.printf("[STOCKS] %-10s $%10.2f  %+.2f (%+.2f%%)\n",
                          sd.stocks[i].display_name,
                          sd.stocks[i].price,
                          sd.stocks[i].change,
                          sd.stocks[i].change_pct);
        }
    }

    sd.valid = anyValid;
    return anyValid;
}
