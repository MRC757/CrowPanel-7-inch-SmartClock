#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// balldontlie.h — one persistent, keep-alive HTTP session shared by the NFL and
// NBA fetchers.
//
// Both endpoints live on the same host (api.balldontlie.io) and are fetched
// back-to-back in the same round.  Previously each did its own one-shot
// request: NFL opened a TLS connection, tore it down, then NBA opened a fresh
// one.  That second handshake needs a ~16 KB contiguous internal-SRAM block
// for the mbedTLS I/O buffer — the allocation that fails with -32512 once the
// heap is fragmented (NBA runs last, so it loses this race first).
//
// The catch that made a shared WiFiClientSecure alone insufficient:
// ~HTTPClient() calls _client->stop() UNCONDITIONALLY (no keep-alive check),
// so a local `HTTPClient http;` in fetchNfl() hard-closes the shared socket
// the moment it goes out of scope.  The fix is to share the HTTPClient too —
// a single file-scope instance that never destructs, so the socket it holds
// open survives from the NFL fetch into the NBA fetch.
//
// Usage: bdl_http().begin(bdl_client(), url); … ; bdl_http().end();
//        NBA (last in the round) then calls bdl_release() to close the socket.
// ─────────────────────────────────────────────────────────────────────────────
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "json_buf.h"   // ssl_prepare()

static WiFiClientSecure _bdl_client;
static HTTPClient       _bdl_http;
static bool             _bdl_ready = false;

static WiFiClientSecure& bdl_client() {
    if (!_bdl_ready) {
        ssl_prepare(_bdl_client);
        _bdl_ready = true;
    }
    return _bdl_client;
}

static HTTPClient& bdl_http() {
    return _bdl_http;
}

// Close the shared socket.  Call after the last fetch of the round (NBA), and
// on any error path so a half-open session isn't reused next hour.
static void bdl_release() {
    _bdl_http.end();
    _bdl_client.stop();
}
