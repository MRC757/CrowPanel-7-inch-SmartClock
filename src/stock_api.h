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
    time_t    fetch_time = 0;   // epoch of last successful fetch (0 = never)
};

// ─── Fetch one symbol via the chart API ───────────────────────────────────────
// Takes a shared WiFiClientSecure so HTTP/1.1 keep-alive can reuse the TLS
// session across sequential same-host requests within a batch.
// Caller must call client.stop() after the entire batch, not between symbols.
static bool fetchStockChart(WiFiClientSecure& client, const char* symbol, StockInfo& si) {
    // URL-encode special characters in ticker symbols:
    //   ^ → %5E  (index symbols: ^GSPC, ^DJI)
    //   = → %3D  (futures symbols: GC=F, SI=F)
    char sym[16];
    strncpy(sym, symbol, sizeof(sym) - 1);
    sym[sizeof(sym) - 1] = '\0';
    // In-place replacement: write encoded form into a separate url buffer.
    char url[128];
    {
        // Build URL without Arduino String heap allocations.
        char encoded[24] = {};
        const char* base = YAHOO_CHART_BASE;
        int ui = 0;
        // Copy base
        for (int i = 0; base[i] && ui < (int)sizeof(url) - 1; i++)
            url[ui++] = base[i];
        // Append URL-encoded symbol
        for (int i = 0; sym[i] && ui < (int)sizeof(url) - 5; i++) {
            if      (sym[i] == '^') { url[ui++]='%'; url[ui++]='5'; url[ui++]='E'; }
            else if (sym[i] == '=') { url[ui++]='%'; url[ui++]='3'; url[ui++]='D'; }
            else                    { url[ui++] = sym[i]; }
        }
        // Append query string
        const char* qs = "?range=1d&interval=1d";
        for (int i = 0; qs[i] && ui < (int)sizeof(url) - 1; i++)
            url[ui++] = qs[i];
        url[ui] = '\0';
    }

    // Filter to just the meta fields we need — parsed directly from the stream,
    // no intermediate String/heap allocation for the response body.
    // chartPreviousClose is always present for both stocks and indices.
    // regularMarketPreviousClose is often absent for index symbols (^GSPC, ^DJI).
    StaticJsonDocument<192> filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"]         = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"]         = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPreviousClose"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"]              = true;

    // static → BSS segment (internal SRAM), not heap/PSRAM.
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
            continue;   // retry
        }

        // Stream directly into the filtered doc — no String/heap allocation for body.
        // http.end() discards any unread bytes so keep-alive stays clean.
        doc.clear();
        DeserializationError err = deserializeJson(doc, http.getStream(),
                                                   DeserializationOption::Filter(filter));
        http.end();
        // Do NOT call client.stop() — keep TLS session alive for next symbol.

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

// ─── Public fetch function — one shared TLS session for all symbols ───────────
// A single WiFiClientSecure is reused across all STOCK_COUNT requests.
// HTTP/1.1 keep-alive means only the FIRST symbol does a full TLS handshake;
// subsequent symbols reuse the existing session — drastically fewer mbedTLS
// alloc/free cycles per batch, which slows SRAM heap fragmentation.
static bool fetchStocks(StocksData& sd) {
    // Always refresh metadata (symbol, display name).
    for (int i = 0; i < STOCK_COUNT; i++) {
        strncpy(sd.stocks[i].symbol,       STOCK_SYMBOLS[i],       sizeof(sd.stocks[i].symbol) - 1);
        strncpy(sd.stocks[i].display_name, STOCK_DISPLAY_NAMES[i], sizeof(sd.stocks[i].display_name) - 1);
    }

    // One client for the entire batch — keep-alive reuses TLS across symbols.
    WiFiClientSecure client;
    client.setInsecure();

    // Fetch each symbol into a temp copy so a failed request leaves the
    // previous valid price visible rather than replacing it with "N/A".
    bool anyNew = false;
    for (int i = 0; i < STOCK_COUNT; i++) {
        StockInfo tmp = sd.stocks[i];
        tmp.valid = false;
        if (fetchStockChart(client, STOCK_SYMBOLS[i], tmp)) {
            sd.stocks[i] = tmp;
            anyNew = true;
            Serial.printf("[STOCKS] %-10s $%10.2f  %+.2f (%+.2f%%)\n",
                          sd.stocks[i].display_name,
                          sd.stocks[i].price,
                          sd.stocks[i].change,
                          sd.stocks[i].change_pct);
        }
        // else: sd.stocks[i] retains its previous price rather than going N/A
    }

    client.stop();   // explicit teardown once after all symbols are done

    if (anyNew) {
        sd.fetch_time = time(nullptr);
        sd.valid = true;
    }
    // sd.valid stays true from a previous successful cycle if this one fails —
    // the preserved prices are still worth displaying.
    return anyNew;
}
