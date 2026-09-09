#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// json_buf.h — single shared JSON parse buffer for all *_api.h fetchers.
//
// Every fetch function parses its HTTP response into this document and copies
// the fields it needs into its own structs before returning.  Fetches run
// strictly sequentially from loop(), so one buffer sized for the largest
// response (Open-Meteo, ~6 KB filtered) safely serves all APIs.
//
// static → BSS segment (internal SRAM), allocated once at link time.
// Replaces seven per-file StaticJsonDocuments totalling 16.4 KB with one
// 8 KB buffer — saves ~8 KB of internal SRAM.
// ─────────────────────────────────────────────────────────────────────────────
#include <ArduinoJson.h>
#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

static StaticJsonDocument<8192> g_json_doc;

// ─────────────────────────────────────────────────────────────────────────────
// ssl_prepare() — standard TLS setup for every fetcher in this project.
//
//  • setInsecure() — no cert validation (all endpoints serve public data).
//
//  NOTE: shrinking the mbedTLS I/O buffers (the 16 KB RX allocation is what
//  fails with -32512 under SRAM fragmentation) is NOT possible on this core —
//  NetworkClientSecure exposes no setBufferSizes(), and MBEDTLS_SSL_IN_CONTENT_LEN
//  is baked into the precompiled framework.  Changing it would require an
//  espidf framework build.  Centralised here so that lever lands in one place
//  if the platform ever gains the API.
// ─────────────────────────────────────────────────────────────────────────────
static inline void ssl_prepare(WiFiClientSecure& c) {
    c.setInsecure();
}

// ─────────────────────────────────────────────────────────────────────────────
// BlockingStream — adapter that lets ArduinoJson deserialize straight off the
// TLS socket instead of buffering the whole HTTP body into a heap String first.
//
// Why it's needed: ArduinoJson's filtering deserializer pulls the response one
// byte at a time via Stream::read().  WiFiClientSecure delivers TLS data in
// records; between records read() briefly returns -1, which ArduinoJson treats
// as end-of-input — silently truncating any response larger than one record
// (the failure weather_api.h works around by buffering to a String).
//
// This wrapper waits out those gaps: it reports EOF only when the body is
// actually complete.  Peak memory during a fetch drops from "entire raw
// response" to just the shared g_json_doc plus the driver's TLS buffers —
// which is what keeps large in-season sports responses from exhausting SRAM.
// ─────────────────────────────────────────────────────────────────────────────
//
// Framing (chosen from what HTTPClient reports after GET()):
//   • len = -1, chunked = false : read until socket close / idle_ms
//       (HTTP/1.0 or Connection: close — one-shot connections).
//   • len >= 0, chunked = false : read exactly len bytes then stop.
//       REQUIRED on a keep-alive socket, which stays open past the body.
//   • chunked = true            : strip hex chunk-size lines + CRLFs, stop
//       at the 0-chunk (HTTP/1.1 keep-alive against Cloudflare, which chunks).
class BlockingStream : public Stream {
    WiFiClient& _c;
    long        _remaining;   // identity: bytes left; -1 = until close
    bool        _chunked;
    long        _chunk_left;  // chunked: bytes left in chunk; 0 = read header; -1 = done
    uint32_t    _idle_ms;

    int _raw() {
        uint32_t t0 = millis();
        for (;;) {
            int ch = _c.read();
            if (ch >= 0) return ch;
            if (!_c.connected() && _c.available() == 0) return -1;
            if (millis() - t0 > _idle_ms) return -1;
            delay(1);
        }
    }
public:
    explicit BlockingStream(WiFiClient& c, long len = -1, bool chunked = false,
                            uint32_t idle_ms = 4000)
        : _c(c), _remaining(len), _chunked(chunked),
          _chunk_left(0), _idle_ms(idle_ms) {}

    int read() override {
        if (_chunked) {
            if (_chunk_left == -1) return -1;
            if (_chunk_left == 0) {
                char line[12]; int n = 0, ch;
                while ((ch = _raw()) >= 0 && ch != '\n')
                    if (n < 11 && ch != '\r') line[n++] = (char)ch;
                line[n] = '\0';
                long sz = strtol(line, nullptr, 16);
                if (sz <= 0) { _chunk_left = -1; return -1; }   // final 0-chunk
                _chunk_left = sz;
            }
            int ch = _raw();
            if (ch >= 0 && --_chunk_left == 0) { _raw(); _raw(); }  // eat CRLF
            return ch;
        }
        if (_remaining == 0) return -1;
        int ch = _raw();
        if (ch >= 0 && _remaining > 0) _remaining--;
        return ch;
    }
    size_t readBytes(char* buffer, size_t length) override {
        size_t got = 0;
        while (got < length) {
            int c = read();
            if (c < 0) break;
            buffer[got++] = (char)c;
        }
        return got;
    }
    int    available() override      { return _c.available(); }
    int    peek() override           { return _c.peek(); }
    size_t write(uint8_t b) override  { return _c.write(b); }
    void   flush() override           {}
};
