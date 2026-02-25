#pragma once

// ─── Display ──────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// ─── I2C (shared by TCA9534 backlight and GT911 touch) ────────────────────────
#define I2C_SDA_PIN  15
#define I2C_SCL_PIN  16

// ─── TCA9534 I/O expander (controls backlight via I2C) ────────────────────────
// Address 0x18; pin 1 = backlight enable (HIGH = on)
#define TCA9534_I2C_ADDR  0x18
#define TCA9534_REG_OUT   0x01   // Output register
#define TCA9534_REG_CFG   0x03   // Configuration register (0=output, 1=input)
#define TCA9534_BL_BIT    1      // Bit index for backlight pin

// ─── GT911 Capacitive Touch — configured inside LGFX_Driver.h (LovyanGFX Touch_GT911) ───

// ─── Screen IDs (used by navigateTo()) ────────────────────────────────────────
#define SCR_SETUP    0
#define SCR_MAIN     1
#define SCR_NEWS     2
#define SCR_STOCKS   3
#define SCR_FORECAST 4
#define SCR_NFL      5
#define SCR_HOURLY   6

// ─── Data update intervals (milliseconds) ─────────────────────────────────────
#define WEATHER_UPDATE_MS   (15UL * 60 * 1000)   // 15 minutes
#define NEWS_UPDATE_MS      (30UL * 60 * 1000)   // 30 minutes
#define STOCKS_UPDATE_MS    ( 5UL * 60 * 1000)   //  5 minutes
#define NTP_SYNC_MS         (60UL * 60 * 1000)   //  1 hour
#define ISS_UPDATE_MS       ( 6UL * 60 * 60 * 1000)  //  6 hours (passes are predictable)
#define ALERTS_UPDATE_MS    ( 5UL * 60 * 1000)   //  5 minutes (safety-critical)
#define NFL_UPDATE_MS       (60UL * 60 * 1000)   //  1 hour

// ─── NTP ──────────────────────────────────────────────────────────────────────
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "time.nist.gov"

// ─── Stock definitions ────────────────────────────────────────────────────────
#define STOCK_COUNT  6
// Yahoo Finance symbols (^ → %5E, = → %3D for URL encoding)
#define STOCK_SYMBOLS_URL  "%5EGSPC,%5EDJI,VYMI,VYM,GC%3DF,SI%3DF"
static const char* STOCK_DISPLAY_NAMES[STOCK_COUNT] = {
    "S&P 500", "DOW JONES", "VYMI", "VYM", "GOLD", "SILVER"
};
static const char* STOCK_SYMBOLS[STOCK_COUNT] = {
    "^GSPC", "^DJI", "VYMI", "VYM", "GC=F", "SI=F"
};

// ─── Webz.io News API Lite ────────────────────────────────────────────────────
// Filters: English, breaking:true
// IPTC categories: weather, economy, science
// NOTE: News API Lite returns HTTP 500 with 4+ OR conditions in one group;
//       maximum 3 categories supported.
#define WEBZ_API_KEY   "0721b064-d7af-40e4-9e17-ca7e3604dec2"
#define WEBZ_NEWS_URL \
    "https://api.webz.io/newsApiLite" \
    "?token=" WEBZ_API_KEY \
    "&q=language%3Aenglish%20AND%20" \
    "%28category%3Aweather%20OR%20" \
    "category%3Aeconomy%20OR%20" \
    "category%3Ascience%29%20AND%20breaking%3Atrue"
#define NEWS_MAX_HEADLINES  10
#define NEWS_HEADLINE_LEN   128

// ─── API base URLs ────────────────────────────────────────────────────────────
#define ZIPPOPOTAM_BASE       "https://api.zippopotam.us/us/"
#define OPEN_METEO_BASE       "https://api.open-meteo.com/v1/forecast"
#define YAHOO_CHART_BASE      "https://query2.finance.yahoo.com/v8/finance/chart/"
#define BALLDONTLIE_NFL_BASE  "https://api.balldontlie.io/nfl/v1/games"
#define N2YO_PASSES_BASE      "https://api.n2yo.com/rest/v1/satellite/visualpasses/25544/"

// ─── Default timezone offset (seconds). Overwritten by Open-Meteo response. ──
#define DEFAULT_UTC_OFFSET_SEC  -18000   // UTC-5 (US Eastern Standard Time)

// Forward declaration — implemented in main.cpp
void navigateTo(int screenId);
