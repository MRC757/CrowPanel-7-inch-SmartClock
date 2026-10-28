#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// app_state.h — single source of truth for state shared across main.cpp,
// wifi_mgr.h, ntp_mgr.h, and fetch_mgr.h.
//
// Split out of main.cpp (which used to hold ~926 lines of globals + WiFi
// lifecycle + NTP + fetch scheduling all in one file) to separate "what the
// data is" from "what touches the network." Pure code motion — every symbol
// here previously lived as a file-scope global/static in main.cpp, with the
// exact same linkage, and nothing here changes behavior.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "prefs_mgr.h"
#include "weather_api.h"
#include "stock_api.h"
#include "iss_api.h"
#include "alerts_api.h"
#include "nfl_api.h"
#include "nba_api.h"

// ─── Preferences + fetched data ────────────────────────────────────────────
AppPrefs    g_prefs;
WeatherData g_weather = {};
StocksData  g_stocks  = {};
IssData     g_iss     = {};
AlertsData  g_alerts  = {};
NflData     g_nfl     = {};
NbaData     g_nba     = {};

// ─── Active LVGL screen objects ────────────────────────────────────────────
static lv_obj_t* scr_setup     = nullptr;
static lv_obj_t* scr_main      = nullptr;
static lv_obj_t* scr_stocks    = nullptr;
static lv_obj_t* scr_forecast  = nullptr;
static lv_obj_t* scr_hourly    = nullptr;
static lv_obj_t* scr_nfl       = nullptr;
static lv_obj_t* scr_nba       = nullptr;
static lv_obj_t* scr_countdown = nullptr;

// ─── Timing (millis() of last successful/attempted action) ────────────────
static unsigned long last_weather_ms   = 0;
static unsigned long last_stocks_ms    = 0;
static unsigned long last_ntp_ms       = 0;
static unsigned long last_iss_ms       = 0;
static unsigned long last_alerts_ms    = 0;
static unsigned long last_nfl_ms       = 0;
static unsigned long last_nba_ms       = 0;
static unsigned long last_dim_check_ms = 0;  // auto-dim evaluation interval
static unsigned long last_reconnect_ms = 0;  // WiFi reconnect watchdog

// Alert state — track a hash of active Extreme/Severe events so we only
// buzz when the alert set genuinely changes (not on every 5-min re-fetch).
static uint32_t last_alert_hash = 0;

// ─── State flags ────────────────────────────────────────────────────────────
static bool wifi_connected   = false;
static bool first_fetch_done = false;
static bool rtc_available    = false;   // true if BM8563 responds on I2C
