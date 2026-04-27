#pragma once

// ─── Debug logging ────────────────────────────────────────────────────────────
// Set to 1 to enable verbose API diagnostic output (raw body dumps, etc.).
// Set to 0 for production builds to keep serial output clean.
#define SMART_CLOCK_DEBUG  0

// ─── Display ──────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// ─── I2C (SDA=15, SCL=16 — shared by STC8H backlight/buzzer MCU at 0x30, BM8563 RTC at 0x51, GT911 touch at 0x5D) ───
#define I2C_SDA_PIN  15
#define I2C_SCL_PIN  16

// ─── Screen IDs (used by navigateTo()) ────────────────────────────────────────
#define SCR_SETUP    0
#define SCR_MAIN     1
#define SCR_NEWS     2
#define SCR_STOCKS   3
#define SCR_FORECAST 4
#define SCR_NFL      5
#define SCR_HOURLY   6
#define SCR_NBA      7

// ─── Data update intervals (milliseconds) ─────────────────────────────────────
#define WEATHER_UPDATE_MS   (60UL * 60 * 1000)        //  1 hour
#define NEWS_UPDATE_MS      (30UL * 60 * 1000)        // 30 minutes (Google News RSS — no rate limit)
#define STOCKS_UPDATE_MS    ( 5UL * 60 * 1000)        //  5 minutes
#define NTP_SYNC_MS         (60UL * 60 * 1000)        //  1 hour
#define ISS_UPDATE_MS       ( 6UL * 60 * 60 * 1000)  //  6 hours (passes are predictable)
#define ALERTS_UPDATE_MS    ( 5UL * 60 * 1000)        //  5 minutes (safety-critical)
#define NFL_UPDATE_MS       (60UL * 60 * 1000)        //  1 hour
#define NBA_UPDATE_MS       (60UL * 60 * 1000)        //  1 hour

// ─── NTP ──────────────────────────────────────────────────────────────────────
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "time.nist.gov"

// ─── Stock definitions ────────────────────────────────────────────────────────
#define STOCK_COUNT  6
// Default Yahoo Finance symbols — used only to pre-populate NVS on first boot.
// Actual symbols used at runtime come from g_prefs.stock_symbols / stock_names.
static const char* STOCK_SYMBOLS_DEFAULT[STOCK_COUNT] = {
    "^GSPC", "^DJI", "VYMI", "VYM", "GC=F", "SI=F"
};
static const char* STOCK_NAMES_DEFAULT[STOCK_COUNT] = {
    "S&P 500", "DOW JONES", "VYMI", "VYM", "GOLD", "SILVER"
};

// ─── Google News RSS ──────────────────────────────────────────────────────────
// URL defined in news_api.h. No API key required.
#define NEWS_MAX_HEADLINES  12
#define NEWS_HEADLINE_LEN   128

// ─── API base URLs ────────────────────────────────────────────────────────────
#define ZIPPOPOTAM_BASE       "https://api.zippopotam.us/us/"
#define OPEN_METEO_BASE       "https://api.open-meteo.com/v1/forecast"
#define YAHOO_CHART_BASE      "https://query2.finance.yahoo.com/v8/finance/chart/"
#define BALLDONTLIE_NFL_BASE  "https://api.balldontlie.io/nfl/v1/games"
#define BALLDONTLIE_NBA_BASE  "https://api.balldontlie.io/v1/games"
#define N2YO_PASSES_BASE      "https://api.n2yo.com/rest/v1/satellite/visualpasses/25544/"

// ─── Default timezone offset (seconds). Overwritten by Open-Meteo response. ──
#define DEFAULT_UTC_OFFSET_SEC  -18000   // UTC-5 (US Eastern Standard Time)

// Forward declaration — implemented in main.cpp
void navigateTo(int screenId);
