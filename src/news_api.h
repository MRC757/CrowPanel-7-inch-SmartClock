#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// news_api.h — Google News RSS feed, top US headlines
//
// Endpoint : https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en
// Format   : RSS 2.0 (XML)
// Parses   : <item><title>…</title></item> — up to NEWS_MAX_HEADLINES items
// No API key required; no rate limit.
//
// Streaming XML parse: reads character data into fixed stack buffers so the
// full response body is never buffered into a single heap allocation.
// CDATA sections (<![CDATA[…]]>) are handled for feeds that use them.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"

#define GOOGLE_NEWS_RSS \
    "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"

struct NewsData {
    char headlines[NEWS_MAX_HEADLINES][NEWS_HEADLINE_LEN];
    int  count;
    bool valid;
};

// ─── UTF-8 → ASCII transliterator (in-place) ─────────────────────────────────
// Google News RSS sends raw UTF-8 multi-byte sequences for smart quotes, dashes,
// ellipsis, and accented letters.  The display font is ASCII-only, so replace
// known sequences with printable equivalents and drop everything else.
// Works in-place: write pointer never overtakes read pointer.
static void _utf8ToAscii(char* s) {
    auto* r = (unsigned char*)s;
    char* w = s;
    while (*r) {
        if (*r < 0x80) {
            // Plain ASCII — pass through.
            *w++ = (char)*r++;
        } else if ((*r & 0xE0) == 0xC0 &&
                   *(r+1) && (*(r+1) & 0xC0) == 0x80) {
            // 2-byte sequence (U+0080–U+07FF): Latin-1 supplement + Latin Extended.
            uint16_t cp = (uint16_t)((*r & 0x1F) << 6) | (*(r+1) & 0x3F);
            r += 2;
            if      (cp >= 0xC0 && cp <= 0xC6) *w++ = 'A';
            else if (cp == 0xC7)               *w++ = 'C';
            else if (cp >= 0xC8 && cp <= 0xCB) *w++ = 'E';
            else if (cp >= 0xCC && cp <= 0xCF) *w++ = 'I';
            else if (cp == 0xD1)               *w++ = 'N';
            else if (cp >= 0xD2 && cp <= 0xD6) *w++ = 'O';
            else if (cp >= 0xD9 && cp <= 0xDC) *w++ = 'U';
            else if (cp >= 0xE0 && cp <= 0xE6) *w++ = 'a';
            else if (cp == 0xE7)               *w++ = 'c';
            else if (cp >= 0xE8 && cp <= 0xEB) *w++ = 'e';
            else if (cp >= 0xEC && cp <= 0xEF) *w++ = 'i';
            else if (cp == 0xF1)               *w++ = 'n';
            else if (cp >= 0xF2 && cp <= 0xF6) *w++ = 'o';
            else if (cp >= 0xF9 && cp <= 0xFC) *w++ = 'u';
            else if (cp == 0xA0)               *w++ = ' ';  // non-breaking space
            // else: drop unmapped 2-byte chars
        } else if ((*r & 0xF0) == 0xE0 &&
                   *(r+1) && (*(r+1) & 0xC0) == 0x80 &&
                   *(r+2) && (*(r+2) & 0xC0) == 0x80) {
            // 3-byte sequence (U+0800–U+FFFF): covers smart quotes, dashes, ellipsis.
            uint16_t cp = (uint16_t)((*r & 0x0F) << 12) |
                          (uint16_t)((*(r+1) & 0x3F) << 6) |
                          (*(r+2) & 0x3F);
            r += 3;
            switch (cp) {
                case 0x2018: case 0x2019:
                case 0x201A: case 0x201B: *w++ = '\''; break;  // smart single quotes
                case 0x201C: case 0x201D:
                case 0x201E:              *w++ = '"';  break;  // smart double quotes
                case 0x2013:              *w++ = '-';  break;  // en dash
                case 0x2014:              *w++ = '-';  break;  // em dash
                case 0x2026: *w++='.'; *w++='.'; *w++='.'; break; // ellipsis (3 bytes → 3 bytes)
                case 0x00A0:              *w++ = ' ';  break;  // non-breaking space
                // else: drop unknown 3-byte chars (e.g. CJK, symbols)
            }
        } else if ((*r & 0xF8) == 0xF0) {
            r += 4;   // drop 4-byte sequences (emoji etc.)
        } else {
            r++;      // drop stray continuation bytes
        }
    }
    *w = '\0';
}

// ─── HTML entity decoder (in-place) ──────────────────────────────────────────
// Converts &amp; &#8217; etc. to their ASCII equivalents so titles render
// cleanly on the display.
static void _decodeEntities(char* s) {
    static const struct { const char* e; char c; } map[] = {
        {"amp",   '&'}, {"lt",    '<'}, {"gt",    '>'},
        {"quot",  '"'}, {"apos",  '\''}, {"#39",  '\''},
        {"#8216", '\''}, {"#8217", '\''}, {"#8218", '\''},
        {"#8220", '"'},  {"#8221", '"'},
        {"#8211", '-'},  {"#8212", '-'},
        {"#160",  ' '},
    };
    char* r = s;
    char* w = s;
    while (*r) {
        if (*r == '&') {
            char* end = strchr(r + 1, ';');
            if (end && end - r <= 9) {
                char ent[9] = {};
                strncpy(ent, r + 1, end - r - 1);
                bool hit = false;
                for (auto& m : map) {
                    if (strcmp(ent, m.e) == 0) {
                        *w++ = m.c;
                        r    = end + 1;
                        hit  = true;
                        break;
                    }
                }
                if (!hit) { *w++ = *r++; }
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

// ─── Fixed-buffer stream readers (no heap String churn) ──────────────────────
// Reads until 'delim' (consumed, not stored), disconnect, or 8 s idle timeout.
// Stores up to cap-1 chars NUL-terminated in buf; counts ALL chars consumed so
// the caller can apply the same oversize checks the String version used.
// 'found' reports whether the delimiter was actually reached.
static size_t _readUntilChar(WiFiClient& s, char delim, char* buf, size_t cap,
                              bool& found) {
    size_t total = 0, n = 0;
    unsigned long last_rx = millis();
    found = false;
    while (millis() - last_rx < 8000UL) {
        int c = s.read();
        if (c < 0) {
            if (!s.connected() && !s.available()) break;
            delay(2);
            continue;
        }
        last_rx = millis();
        if ((char)c == delim) { found = true; break; }
        total++;
        if (n < cap - 1) buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return total;
}

// Trim leading/trailing whitespace in place.
static void _trimInPlace(char* s) {
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len-1] == ' '  || p[len-1] == '\t' ||
                       p[len-1] == '\r' || p[len-1] == '\n')) p[--len] = '\0';
    if (p != s) memmove(s, p, len + 1);
}

// ─── Fetch top headlines from Google News RSS ─────────────────────────────────
static bool fetchNews(NewsData& nd) {
    nd.count = 0;
    nd.valid = false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, GOOGLE_NEWS_RSS);
    http.setTimeout(20000);
    http.addHeader("User-Agent", "SmartClock/1.0 ESP32");

    int code = http.GET();
    Serial.printf("[NEWS] HTTP %d\n", code);
    if (code != 200) {
        http.end();
        client.stop();
        return false;
    }

    WiFiClient& stream = http.getStream();

    // State machine: find <item>, then capture text between <title></title>.
    // Fixed stack buffers — zero heap allocation during the parse.
    enum { SEEK_ITEM, IN_ITEM, IN_TITLE } state = SEEK_ITEM;
    char text_buf[NEWS_HEADLINE_LEN + 1];
    char tag_buf[336];   // fits "![CDATA[" + 128-char headline + "]]" with room
    bool found;

    while (nd.count < NEWS_MAX_HEADLINES) {
        // Read text content up to the next '<'
        size_t text_len = _readUntilChar(stream, '<', text_buf,
                                          sizeof(text_buf), found);
        if (!found) break;                                 // stream dead or idle
        if (text_len > NEWS_HEADLINE_LEN) text_buf[0] = '\0';  // oversized — discard

        // Read tag (or CDATA marker) up to the closing '>'
        size_t tag_len = _readUntilChar(stream, '>', tag_buf,
                                         sizeof(tag_buf), found);
        if (!found || tag_len == 0) break;
        if (tag_len > 512) continue;  // malformed/oversized tag — skip

        // ── CDATA section: <![CDATA[text]]> ──────────────────────────────
        // 'tag_buf' holds everything between '<' and '>', so for a CDATA
        // block that is "![CDATA[title text]]".
        if (state == IN_TITLE && strncmp(tag_buf, "![CDATA[", 8) == 0) {
            char* val = tag_buf + 8;                       // strip "![CDATA["
            size_t vlen = strlen(val);
            if (vlen >= 2 && val[vlen-2] == ']' && val[vlen-1] == ']')
                val[vlen-2] = '\0';
            _trimInPlace(val);
            if (val[0]) {
                strlcpy(nd.headlines[nd.count], val, NEWS_HEADLINE_LEN);
                _utf8ToAscii(nd.headlines[nd.count]);
                _decodeEntities(nd.headlines[nd.count]);
                nd.count++;
            }
            state = IN_ITEM;
            continue;
        }

        // ── Normal XML tag ───────────────────────────────────────────────
        // Drop any attributes: "item isPermaLink=…" → "item"
        char* sp = strchr(tag_buf, ' ');
        if (sp) *sp = '\0';

        if      (strcmp(tag_buf, "item")   == 0)                     { state = IN_ITEM;  }
        else if (strcmp(tag_buf, "/item")  == 0)                     { state = SEEK_ITEM; }
        else if (strcmp(tag_buf, "title")  == 0 && state == IN_ITEM) { state = IN_TITLE; }
        else if (strcmp(tag_buf, "/title") == 0 && state == IN_TITLE) {
            _trimInPlace(text_buf);
            if (text_buf[0]) {
                strlcpy(nd.headlines[nd.count], text_buf, NEWS_HEADLINE_LEN);
                _utf8ToAscii(nd.headlines[nd.count]);
                _decodeEntities(nd.headlines[nd.count]);
                nd.count++;
            }
            state = IN_ITEM;
        }
    }

    http.end();
    client.stop();

    nd.valid = (nd.count > 0);
    Serial.printf("[NEWS] Got %d headlines\n", nd.count);
    return nd.valid;
}
