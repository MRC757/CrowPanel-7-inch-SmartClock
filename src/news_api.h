#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// news_api.h — Google News RSS feed, top US headlines
//
// Endpoint : https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en
// Format   : RSS 2.0 (XML)
// Parses   : <item><title>…</title></item> — up to NEWS_MAX_HEADLINES items
// No API key required; no rate limit.
//
// Streaming XML parse: reads character data with readStringUntil() so the
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
    stream.setTimeout(8000);  // 8 s per readStringUntil call

    // State machine: find <item>, then capture text between <title></title>.
    enum { SEEK_ITEM, IN_ITEM, IN_TITLE } state = SEEK_ITEM;

    while (nd.count < NEWS_MAX_HEADLINES) {
        // Read text content up to the next '<'
        String text = stream.readStringUntil('<');
        if (!stream.connected() && text.length() == 0) break;
        if (text.length() > NEWS_HEADLINE_LEN) text = "";  // oversized inter-tag text — discard

        // Read tag (or CDATA marker) up to the closing '>'
        String tag = stream.readStringUntil('>');
        if (tag.length() == 0) break;
        if (tag.length() > 512) continue;  // malformed/oversized tag — skip

        // ── CDATA section: <![CDATA[text]]> ──────────────────────────────
        // 'tag' here contains everything between '<' and '>', so for a CDATA
        // block that is "![CDATA[title text]]".
        if (tag.startsWith("![CDATA[") && state == IN_TITLE) {
            String val = tag.substring(8);               // strip "![CDATA["
            if (val.endsWith("]]")) val.remove(val.length() - 2);
            val.trim();
            if (val.length() > 0) {
                strlcpy(nd.headlines[nd.count], val.c_str(), NEWS_HEADLINE_LEN);
                _utf8ToAscii(nd.headlines[nd.count]);
                _decodeEntities(nd.headlines[nd.count]);
                nd.count++;
            }
            state = IN_ITEM;
            continue;
        }

        // ── Normal XML tag ───────────────────────────────────────────────
        // Drop any attributes: "item isPermaLink=…" → "item"
        int sp = tag.indexOf(' ');
        String name = (sp > 0) ? tag.substring(0, sp) : tag;

        if      (name == "item")                         { state = IN_ITEM;  }
        else if (name == "/item")                        { state = SEEK_ITEM; }
        else if (name == "title"   && state == IN_ITEM)  { state = IN_TITLE; }
        else if (name == "/title"  && state == IN_TITLE) {
            text.trim();
            if (text.length() > 0) {
                strlcpy(nd.headlines[nd.count], text.c_str(), NEWS_HEADLINE_LEN);
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
