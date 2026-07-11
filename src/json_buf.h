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

static StaticJsonDocument<8192> g_json_doc;
