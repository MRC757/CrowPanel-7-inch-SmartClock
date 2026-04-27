# Smart Clock

A full-featured smart clock display for the **Elecrow CrowPanel Advance 7.0 HMI ESP32-S3** *(SKU: DIS02170A, V1.3)* (800×480 IPS touchscreen). Displays local time, weather, 5-day forecast, 3-day hourly charts, market data, live breaking news, ISS pass times, weather alerts, NFL scores, NBA scores for any two configurable teams, and random dad jokes — with automatic night dimming, audible severe-weather alerts, and configurable stock symbols and NBA teams via the on-screen setup.

![Smart Clock Layout](docs/layout.png)
> *(screenshot placeholder — add your own after first boot)*

---

## Features

| Feature | Details |
|---|---|
| **Clock** | Local time & date + UTC time via NTP; auto-detects timezone from ZIP code |
| **Hardware RTC** | BM8563 battery-backed RTC (I2C 0x51) keeps accurate time across power cycles; clock shows correct time instantly on boot |
| **Weather** | Temperature, condition, feels-like, humidity, wind speed; updated every hour |
| **Sunrise & Sunset** | Today's rise/set times in the weather panel (from Open-Meteo, no extra request) |
| **Moon Phase** | Current lunar phase calculated locally — no network request |
| **5-Day Forecast** | Dedicated screen: daily high/low, condition, precipitation, UV index per day |
| **3-Day Hourly Charts** | Scrollable temperature, wind speed, and precip-chance line charts across 72 hours; semi-transparent area shading below each line; auto-scrolls so current time is always at the left edge; Midnight/Noon markers and Y-axis labels |
| **UV Index** | Daily maximum UV index on every forecast card |
| **ISS Pass Times** | Next 3 ISS visible passes scrolling at the bottom of the Forecast screen |
| **Weather Alerts** | NWS active alerts (tornado, flood, severe storm) shown as a red banner across all screens |
| **Alert Buzzer** | Piezo buzzer sounds 5 beeps for Extreme alerts, 3 beeps for Severe — only on new/changed alerts |
| **Market Data** | S&P 500, DOW Jones, VYMI, VYM, Gold (GC=F), Silver (SI=F) — price, change, change % |
| **News** | Google News RSS — up to 12 top US breaking headlines, refreshed every 30 minutes; no API key required |
| **NFL Schedule** | Next 7 days of NFL games: teams, kickoff time, live scores, final scores |
| **NBA Schedule** | Next 7 days of games for any two configurable NBA teams: tip-off time, live quarter scores, final scores, postponements; team-color accent strip per row |
| **Joke Screen** | Random dad joke from RapidAPI Dad Jokes; two-part jokes show setup (white) + punchline (gold); refreshed every 3 hours |
| **Auto Night Dim** | Display automatically dims at sunset and brightens at sunrise; configurable brightness |
| **Hardware Watchdog** | 30-second ESP32 task watchdog resets the device if the main loop hangs in a stalled HTTP connection |
| **Touch Setup** | Two-tab setup screen: Tab 1 — WiFi credentials, ZIP code, WiFi scanner, brightness slider; Tab 2 — configurable stock symbols/names and NBA team selection |
| **Multi-screen** | Setup · Clock · News · Stocks · Daily Forecast · Hourly · NFL · NBA · Joke; tap nav bar to switch |
| **Persistent settings** | WiFi + ZIP + night brightness + stock symbols + NBA team IDs stored in NVS flash; auto-reconnects on boot |

---

## Hardware Required

| Component | Details |
|---|---|
| **Board** | Elecrow CrowPanel Advance 7.0 HMI ESP32-S3 — SKU: DIS02170A, V1.3 |
| **Display** | 7" IPS, 800×480, 16-bit color, RGB parallel interface |
| **Touch** | GT911 capacitive (I2C) |
| **Backlight + Buzzer** | STC8H1K28 co-processor (I2C addr 0x30) |
| **RTC** | BM8563 real-time clock (I2C addr 0x51, PCF8563-compatible, battery-backed) |
| **CPU** | ESP32-S3-WROOM-1-N16R8 @ 240 MHz |
| **RAM** | 512 KB SRAM + 8 MB OPI PSRAM (80 MHz) |
| **Flash** | 16 MB |
| **WiFi** | 2.4 GHz 802.11 b/g/n (required for data) |

> **Note:** This project uses the **RGB parallel bus driver** (`Panel_RGB`). The manufacturer's included demo template uses the wrong SPI driver (`Panel_ILI9488`) — do not copy that driver into this project.

---

## Hardware Pinout

### I2C Bus

| Signal | GPIO |
|---|---|
| SDA | GPIO 15 |
| SCL | GPIO 16 |
| Bus speed | 400 kHz (fast mode) |

### I2C Devices

| Device | Address | Commands / Notes |
|---|---|---|
| STC8H1K28 co-processor | `0x30` | Backlight max brightness: `0x10` |
| STC8H1K28 co-processor | `0x30` | Buzzer ON: `0xF6` (246) |
| STC8H1K28 co-processor | `0x30` | Buzzer OFF: `0xF7` (247) |
| BM8563 RTC | `0x51` | Battery-backed real-time clock (PCF8563-compatible); stores local time in BCD registers |
| GT911 touch controller | `0x5D` | Capacitive multi-touch; reset() must write CONFIG_FRESH=1 |

> The backlight and piezo buzzer are controlled by sending a single command byte to the STC8H MCU at address `0x30`. The BM8563 RTC at `0x51` keeps accurate time across power cycles via its backup battery.

### RGB Parallel Bus (16-bit)

| Channel | Bit | GPIO |
|---|---|---|
| Blue  B0 | d0  | GPIO 21 |
| Blue  B1 | d1  | GPIO 47 |
| Blue  B2 | d2  | GPIO 48 |
| Blue  B3 | d3  | GPIO 45 |
| Blue  B4 | d4  | GPIO 38 |
| Green G0 | d5  | GPIO  9 |
| Green G1 | d6  | GPIO 10 |
| Green G2 | d7  | GPIO 11 |
| Green G3 | d8  | GPIO 12 |
| Green G4 | d9  | GPIO 13 |
| Green G5 | d10 | GPIO 14 |
| Red   R0 | d11 | GPIO  7 |
| Red   R1 | d12 | GPIO 17 |
| Red   R2 | d13 | GPIO 18 |
| Red   R3 | d14 | GPIO  3 |
| Red   R4 | d15 | GPIO 46 |

### RGB Sync Signals

| Signal | GPIO | Notes |
|---|---|---|
| PCLK | GPIO 39 | 16 MHz pixel clock (`pclk_idle_high = 1`) |
| HSYNC | GPIO 40 | Active low; front=40 / pulse=48 / back=40 clocks |
| VSYNC | GPIO 41 | Active low; front=8 / pulse=4 / back=8 lines |
| DE (H-Enable) | GPIO 42 | Data enable |

> **Timing note:** The large 128-clock horizontal blanking window (40+48+40) gives the DMA 8 µs per line to prefetch from PSRAM — matching Elecrow's official ESPHome configuration and eliminating touch-triggered display jitter.

### Initialization Order

The I2C bus must be initialized before the display. Swapping this order causes conflicts because the STC8H and GT911 will not ACK until the bus is stable:

```
Wire.begin(SDA=15, SCL=16) → Wire.setClock(400 kHz) → GT911.begin(0x5D) → rtc_init(0x51) → backlight_init(0x30, 0x10) → gfx.init()
```

---

## Screens

### Setup Screen
Shown on first boot (or when credentials are missing). The setup screen has two tabs:

**Tab 1 — WiFi & Location**
- Enter your WiFi SSID, password, and 5-digit US ZIP code using the on-screen keyboard.
- Tap **⟳ Scan** to automatically scan for nearby WiFi networks. A scrollable popup lists all found networks with signal strength and security status — tap any row to fill in the SSID automatically.
- Tap **Connect & Save** to connect and begin fetching data.
- The **Night Brightness** slider (10–100%) sets how dim the display becomes between sunset and sunrise.

**Tab 2 — Stocks & Teams**
- Six stock rows, each with a **Symbol** field (Yahoo Finance ticker, e.g. `^GSPC`) and a **Name** field (display label, e.g. `S&P 500`). Tap any field to edit — changes are saved to NVS immediately on defocus.
- Two **NBA Team** dropdowns listing all 30 NBA teams alphabetically. Select any two teams; the NBA screen shows their combined schedule. Changes are saved instantly.

All settings are remembered across reboots via NVS flash.

### Clock Screen (default)
```
┌──────────────────────────────────────────────────────────────┐
│  Beverly Hills, CA     12:34:56       Thu Feb 19 2026        │
│                                       UTC 20:34              │
├──────────────────────┬───────────────────────────────────────┤
│  WEATHER             │  MARKET                               │
│                      │  S&P 500   5,234   +0.45%  ▲         │
│       72°F           │  DOW      39,123   -0.12%  ▼         │
│    Partly Cloudy     │  VYMI       55.32  +0.23%  ▲         │
│  Feels like  70°F    │  VYM       123.45  +0.18%  ▲         │
│  Humidity    45%     │  GOLD    2,950.00  +0.31%  ▲         │
│  Wind      8.0 mph   │  SILVER     32.45  -0.08%  ▼         │
│  Rise:   6:45 AM     │                                       │
│  Set:    5:52 PM     │                                       │
│  Moon: Waxing Gibbous│                                       │
├──────────────────────┴───────────────────────────────────────┤
│  ⚙ Setup  🏠 Clock  ≡ News  ≡ Stocks  ~ Daily  ↺ Hourly  ▶ NFL  ↻ NBA  │
└──────────────────────────────────────────────────────────────────────────┘
```

UTC time updates every second. Timezone is auto-detected from the ZIP code via Open-Meteo (DST-aware). Sunrise/sunset come from the same weather fetch. Moon phase is computed locally.

### Alert Banner (when active)
When the NWS reports active alerts for your location, a red banner appears at the top of **every screen**, and the piezo buzzer sounds:
- **Extreme** alert → 5 beeps (150 ms on / 100 ms off)
- **Severe** alert → 3 beeps (200 ms on / 150 ms off)

The buzzer only sounds when the active alert set changes — repeated 5-minute fetches of the same alert are silent. The banner auto-hides when no active alerts are found. Non-US coordinates return no alerts gracefully.

```
⚠ ALERT  Tornado Warning: A tornado warning has been issued for your county...
```

### News Screen
Full scrollable list of up to 12 breaking headlines from the **Google News RSS** feed (`https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en`). No API key required. Headlines are the current top US stories across all topics. Refreshed every 30 minutes.

### Stocks Screen
3×2 card grid with detailed market info for each of the 6 configurable symbols, including absolute price change, percentage change, and market state (Open / Pre-market / After-hours / Closed). Default symbols are S&P 500, DOW, VYMI, VYM, Gold, Silver — change any of them via **Setup → Tab 2**.

Gold and Silver use Yahoo Finance COMEX front-month futures tickers (`GC=F`, `SI=F`) — the same prices shown on Yahoo Finance's gold/silver quote pages.

### Forecast Screen
5-day weather forecast displayed as five vertical cards, with an ISS pass-time strip below:

```
┌──────────────────────────────────────────────────────────────────────────┐
│  ~ 5-Day Forecast                                         12:34 PM       │
├──────────┬──────────┬──────────┬──────────┬───────────────────────────┤
│  Thu     │  Fri     │  Sat     │  Sun     │  Mon                       │
│ TODAY    │          │          │          │                             │
│ Pt Cld   │ Clear    │ Rainy    │ Showers  │ Clear                       │
│   HI  75°│   HI  78°│   HI  65°│   HI  61°│   HI  70°                   │
│   LO  58°│   LO  62°│   LO  50°│   LO  52°│   LO  55°                   │
│ 0.00 in  │ 0.00 in  │ 0.52 in  │ 0.80 in  │ 0.10 in                     │
│ UV 3.2   │ UV 5.1   │ UV 1.8   │ UV 2.4   │ UV 6.0                      │
├──────────┴──────────┴──────────┴──────────┴───────────────────────────┤
│  ISS  Thu 7:23 PM (5m)  |  Fri 8:45 AM (3m)  |  Sat 6:12 PM (6m)      │
├──────────────────────────────────────────────────────────────────────────┤
│  ⚙ Setup  🏠 Clock  ≡ News  ≡ Stocks  ~ Forecast  ▶ NFL               │
└──────────────────────────────────────────────────────────────────────────┘
```

### Hourly Forecast Screen
72-hour (3-day) line chart view with three stacked charts: Temperature (°F), Wind Speed (mph), and Precip Chance (%). Each chart has:
- Standalone Y-axis labels at fixed screen positions
- Midnight (blue) and Noon (gold) vertical marker lines with "12A"/"12P" labels
- Day-of-week labels aligned above each noon marker (correct regardless of time of day)
- **Semi-transparent area fill** below each line — drawn via `LV_EVENT_DRAW_MAIN` callback with subdivided rects for smooth diagonal edges
- **Smooth auto-scroll** — within each hour the charts slide left so the current time is always at the left edge; snaps back on hourly data refresh

### NFL Screen
Shows NFL games scheduled over the next 7 days, grouped by date. Scrollable when more games are listed than fit on screen.

- **Upcoming games** — kickoff time shown in blue (local time)
- **In-progress games** — live score shown in green
- **Final games** — score + "Final" shown in grey

> **Requires a free API key** — see [API Key Setup](#api-key-setup) below.

### NBA Screen
Shows games for your two configured NBA teams over the next 7 days, grouped by date. Scrollable. Select any two teams via **Setup → Tab 2** (defaults: LA Lakers and Golden State Warriors).

- **Upcoming games** — tip-off time shown in gold (local time)
- **In-progress games** — live score + period shown in green (e.g. `98-87 3Q`)
- **Final games** — score + "Final" shown in grey
- **Postponed games** — "Postponed" shown in orange
- **Team accent strip** — 4px left border colored per team; gold when both configured teams play each other

> **Requires the same Ball Don't Lie API key as NFL** — see [API Key Setup](#api-key-setup) below.

### Joke Screen

```
┌──────────────────────────────────────────────────────────────────────────┐
│  ⚡ Joke of the Moment                               Updated 2:15 PM     │ h=44
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│         Setup / question  (white, centered)                              │ h=203
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│         Punchline  (gold, centered)                                      │ h=203
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│  nav bar                                                                 │ h=30
└──────────────────────────────────────────────────────────────────────────┘
```

Displays a random dad joke from the [RapidAPI Dad Jokes API](https://rapidapi.com/KegenGuyll/api/dad-jokes). Two-part jokes show the setup in white at the top and the punchline in gold at the bottom. The "Updated" timestamp in the header ages from grey → orange → red to indicate staleness. Refreshes every 3 hours. No PSRAM usage — text only.

> **Requires a free RapidAPI key with the Dad Jokes API subscribed** — see [API Key Setup](#api-key-setup) below.

---

## API Key Setup

### News — Google News RSS

The News screen fetches the [Google News RSS](https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en) feed. **No API key or account required.** The feed URL and locale are defined in `src/news_api.h` as `GOOGLE_NEWS_RSS`.

### NFL & NBA — Ball Don't Lie

Both the NFL and NBA screens use the [Ball Don't Lie](https://www.balldontlie.io) API (free tier, no cost) with the **same single API key**.

- **NFL endpoint:** `GET https://api.balldontlie.io/nfl/v1/games` — filtered by date (next 7 days)
- **NBA endpoint:** `GET https://api.balldontlie.io/v1/games` — filtered by `team_ids[]=<team1>&team_ids[]=<team2>` (configurable via Setup), plus `start_date`/`end_date`

1. Register at [balldontlie.io](https://www.balldontlie.io) and copy your API key
2. Paste it into `include/secrets.h`:
   ```cpp
   #define BALLDONTLIE_API_KEY  "your_key_here"
   ```
3. Re-build and flash the firmware

Without a key both the NFL and NBA screens display a setup reminder instead of game data.

### ISS Pass Times — N2YO

The Forecast screen's ISS strip uses the [N2YO](https://www.n2yo.com) satellite tracking API (free tier, 1 000 requests/hour).

> **Why N2YO?** The original Open Notify `iss-pass.json` endpoint was permanently decommissioned in 2023 and returns HTTP 404. N2YO provides the same pass-prediction data from a maintained service.

1. Register at [n2yo.com/login/register.php](https://www.n2yo.com/login/register.php)
2. Copy your API key from **My Account**
3. Paste it into `include/config.h` or `include/secrets.h`:
   ```cpp
   #define N2YO_API_KEY  "your_key_here"
   ```
4. Re-build and flash the firmware

Without a key the ISS strip shows a registration reminder.

### Joke — RapidAPI Dad Jokes

The Joke screen uses the [Dad Jokes API](https://rapidapi.com/KegenGuyll/api/dad-jokes) on RapidAPI (free tier). At the 3-hour refresh interval the clock uses ≤ 8 requests/day.

- **Endpoint:** `GET https://dad-jokes.p.rapidapi.com/random/joke`
- **Required headers:** `x-rapidapi-host: dad-jokes.p.rapidapi.com` and `x-rapidapi-key: YOUR_KEY`
- Response format: `{"body":[{"setup":"...","punchline":"..."}],"success":true}`
- All jokes are two-part (setup + punchline)

1. Create a free account at [rapidapi.com](https://rapidapi.com)
2. Search for **"Dad Jokes"** and subscribe to the free tier
3. Copy your API key from the RapidAPI dashboard
4. Paste it into `include/secrets.h`:
   ```cpp
   #define RAPIDAPI_JOKE_KEY  "your_key_here"
   ```
5. Re-build and flash the firmware

`include/secrets.h` is listed in `.gitignore` — your keys will not be committed if you use git.

---

## Auto Night Dimming

The display automatically dims at sunset and returns to full brightness at sunrise, using the sunrise/sunset times returned by the same weather fetch (no extra API call).

**How it works:** The STC8H1K28 MCU at I2C `0x30` is a digital on/off switch with no hardware PWM. Brightness is applied by scaling the R, G, and B channels of every RGB565 pixel in the LVGL display-flush callback (`backlight.h`). At 100% the scale path is skipped entirely (no performance impact during daytime).

**LV_COLOR_16_SWAP note:** Because `LV_COLOR_16_SWAP=1` is required for this display, the pixel bytes in the LVGL flush buffer are swapped relative to standard RGB565 layout. The brightness scaler calls `__builtin_bswap16()` before extracting channels and again after packing, ensuring correct colors at all brightness levels.

**Configuration:** Use the **Night Brightness** slider on the Setup screen (10–100%). The value is saved to NVS and applied immediately.

---

## Hardware RTC (BM8563)

The CrowPanel Advance 7.0 (SKU: DIS02170A, V1.3) includes a **BM8563** real-time clock (PCF8563-compatible) at I2C address `0x51` with battery backup. This keeps accurate time across full power cycles — no NVS epoch workaround needed.

**How it works:**
- **On boot:** `rtc_restore_system_time()` reads local time from the BM8563, converts it to a UTC epoch (subtracting the saved UTC offset), and calls `settimeofday()`. The clock displays the correct time immediately.
- **After NTP sync:** `rtc_write()` writes the fresh NTP-synchronized local time back to the BM8563 so it stays accurate.
- **VL flag:** If the BM8563's backup battery has failed, bit 7 of the seconds register (the "voltage-low" flag) is set. The driver logs `[RTC] VL flag set` to serial and skips restoring unreliable data.

Serial log entries: `[RTC] BM8563 found at 0x51`, `[RTC] System time restored from BM8563`, and `[RTC] Time written`.

The driver (`src/rtc_bm8563.h`) uses direct Wire register access — no external library required.

---

## Display Stability Notes

The CrowPanel 7" uses an ESP32-S3 RGB parallel panel (`Panel_RGB`). Getting stable, jitter-free output on this hardware required several specific settings:

| Setting | Location | Effect |
|---|---|---|
| `use_psram = 2` | `LGFX_Driver.h` | Double PSRAM framebuffer; Panel_RGB swaps atomically at vsync — LCD_CAM always reads a complete, stable frame |
| `freq_write = 16 MHz` | `LGFX_Driver.h` | 16 MHz is the stable minimum for this panel; lower causes vertical sync loss |
| `pclk_idle_high = 1` | `LGFX_Driver.h` | Required for this panel; prevents pixel glitches on idle clock edges |
| `hsync_front_porch = 40` / `pulse = 48` / `back = 40` | `LGFX_Driver.h` | 128 clocks/line blanking gives DMA 8 µs/line to prefetch from PSRAM — matches Elecrow official ESPHome timing |
| `vsync_front_porch = 8` / `pulse = 4` / `back = 8` | `LGFX_Driver.h` | Vertical blanking per Elecrow specification |
| `Wire.setClock(400000)` | `main.cpp setup()` | GT911 I2C at 400 kHz (fast mode) reduces each touch read from ~1 ms to ~250 µs, well within the blanking window |
| `LV_COLOR_16_SWAP 1` | `lv_conf.h` | LVGL pre-swaps pixel bytes so `pushImageDMA` raw-copies to the Panel_RGB framebuffer in the correct byte order the LCD_CAM expects |
| LVGL render buffers in SRAM | `main.cpp` | Two 20-line (32 KB) SRAM buffers keep CPU compositing off the PSRAM bus; only the final `pushImageDMA` touches PSRAM |
| Per-scanline `startWrite/endWrite` in flush | `main.cpp disp_flush()` | ESP32-S3 D-cache = 32 KB = exact size of one LVGL flush strip. Without `endWrite()` the D-cache is never flushed to PSRAM and the vsync double-buffer swap never fires. Per-row pairs limit each PSRAM writeback burst to ~1.6 KB (25 cache lines) and keep `display()` flip flag set each row (boolean — idempotent). See full history in [Display Jitter Troubleshooting](#display-jitter-troubleshooting) |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` | `sdkconfig.defaults` | Keeps non-SSL `malloc()` allocations ≤4 KB in internal SRAM to reduce PSRAM bus traffic during WiFi and JSON activity. WiFi/LwIP use explicit `MALLOC_CAP_INTERNAL\|DMA` and are unaffected. Larger allocations (ArduinoJson, HTTPClient) go to PSRAM. |
| `static StaticJsonDocument<N>` in API files | all `*_api.h` | JSON parse documents allocated in BSS (internal SRAM) — no heap, no PSRAM bus traffic during JSON parsing |
| `LV_THEME_DEFAULT_TRANSITION_TIME 0` | `lv_conf.h` | Disables LVGL press/focus animations that generate continuous flush calls |
| `LV_THEME_DEFAULT_GROW 0` | `lv_conf.h` | Disables grow-on-press effect for the same reason |

---

## Display Jitter Troubleshooting

Horizontal jitter on this board has three distinct sources, each requiring a separate fix:

| Source | Symptom | Fix |
|---|---|---|
| **D-cache burst on flush** | Jitter on every button/keyboard press | Per-scanline `startWrite/endWrite` in `disp_flush()` — limits each PSRAM burst to ~1.6 KB |
| **WiFi/LwIP DMA during connect & downloads** | Jitter on startup and while fetching data | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` in `sdkconfig.defaults` — keeps non-SSL `malloc()` allocations in internal SRAM |
| **ArduinoJson heap in PSRAM** | Jitter while parsing JSON responses | `static StaticJsonDocument<N>` in all `*_api.h` files — BSS segment = internal SRAM |

### Approaches that did NOT work

| Attempt | Outcome |
|---|---|
| `gfx.startWrite()` once in `setup()`, never `endWrite()` in flush | D-cache never flushed; vsync swap never fires; writes hit the active front buffer → worst-case contention |
| `endWrite()` called **before** the write loop | Flushed previous flush's dirty lines, then wrote new pixels with no flush — pixels always one frame behind |
| Removed `endWrite()` entirely (no start, no end) | No D-cache flush, no buffer swap; user reported this made jitter **worse** |
| `CONFIG_LCD_RGB_BOUNCE_BUFFER_SIZE` build flag | No effect — LovyanGFX uses its own GDMA, not the Espressif `esp_lcd_panel_rgb` driver |
| Upgrading LVGL to 8.3.11 | Not a cause; version difference has no impact on jitter |
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` in `sdkconfig.defaults` | **Causes boot crash** — this is a compile-time option baked into the pre-compiled Arduino-ESP32 mbedTLS library. Setting it via `sdkconfig.defaults` creates a configuration conflict that triggers interrupt WDT timeout on boot. SSL heap exhaustion is instead handled by three layers: (1) `recover_ssl_heap()` proactively cycles WiFi when free SRAM < 70 KB; (2) `do_stocks_fetch()` reactively cycles WiFi + retries if all 6 symbols fail; (3) `ESP.restart()` if retry still fails and free SRAM < 65 KB — BM8563 RTC preserves time and the device recovers in ~10 s with a clean heap. HTTP keep-alive further reduces handshake frequency (1 per batch instead of 6). |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384 / 8192 / 4096` (threshold tuning) | mbedTLS calls `heap_caps_calloc(MALLOC_CAP_INTERNAL)` directly — it **completely bypasses** the `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold. Threshold tuning alone has no effect on SSL heap exhaustion; the real fix is `recover_ssl_heap()` + HTTP keep-alive. |

---

## News (Google News RSS)

The News screen fetches `https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en` — a standard RSS 2.0 feed with no authentication, no rate limit, and no account required.

**Parsing:** The firmware uses a streaming XML state machine (`src/news_api.h`) that reads `<item><title>…</title></item>` blocks directly from the HTTP stream without buffering the full response body. Up to 12 headlines are stored.

**Character handling:** Google News sends raw UTF-8 multi-byte sequences for smart quotes (`'` `"`) and dashes (`–` `—`). The `_utf8ToAscii()` pass converts these to their printable ASCII equivalents before the `_decodeEntities()` pass handles any remaining `&amp;`-style XML entities, ensuring clean display on the ASCII font.

**To change the locale/region:** edit `GOOGLE_NEWS_RSS` in `src/news_api.h`. Google News RSS supports `hl=` (language), `gl=` (country), and `ceid=` parameters.

---

## Data Sources

| Data | API | Key Required | Notes |
|---|---|---|---|
| Time | NTP (`pool.ntp.org`) | No | UTC offset auto-detected from ZIP; DST-aware |
| ZIP → coordinates | [api.zippopotam.us](https://api.zippopotam.us) | No | US ZIP codes only |
| Weather, forecast, UV, sunrise/sunset | [Open-Meteo](https://open-meteo.com) | No | Single request returns all current + daily data; persistent TLS session reused across hourly fetches |
| Moon phase | Calculated locally | — | Synodic period formula; no network required |
| News | [Google News RSS](https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en) | No | RSS 2.0 XML; top US breaking headlines; no rate limit |
| Stocks | Yahoo Finance chart API (v8, per-symbol) | No | 6 sequential HTTPS requests; `chartPreviousClose` used for index/futures % change |
| ISS pass times | [N2YO](https://www.n2yo.com) visual passes API | **Free key** | HTTPS; NORAD ID 25544 (ISS) |
| Weather alerts | [NWS API](https://api.weather.gov/alerts/active) | No | HTTPS, US only; uses lat/lon from geocode |
| NFL schedule & scores | [Ball Don't Lie](https://www.balldontlie.io) | **Free key** | HTTPS; `/nfl/v1/games` filtered by date |
| NBA schedule & scores (2 configurable teams) | [Ball Don't Lie](https://www.balldontlie.io) | **Same key as NFL** | HTTPS; `/v1/games` filtered by configurable `team_ids[]` + date range |
| Random joke | [RapidAPI Dad Jokes](https://rapidapi.com/KegenGuyll/api/dad-jokes) | **Free RapidAPI key** | HTTPS; always two-part (setup + punchline) |

### Update Intervals

| Data | Interval |
|---|---|
| Clock display + UTC + moon phase | Every second |
| Weather + forecast + UV + sunrise/sunset | Every 1 hour |
| Auto night dim check | Every 60 seconds |
| Weather alerts (+ buzzer if new Extreme/Severe) | Every 5 minutes |
| News | Every 30 minutes |
| Stocks (all 6 symbols) | Every 5 minutes |
| BM8563 RTC updated | After each NTP sync (every 1 hour) |
| ISS pass times | Every 6 hours |
| NFL schedule | Every 1 hour |
| NBA schedule (2 configurable teams) | Every 1 hour |
| Joke | Every 3 hours |
| NTP re-sync | Every 1 hour |

---

## Software Dependencies

All managed automatically by PlatformIO except TAMC_GT911:

| Library | Version | Use |
|---|---|---|
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | 8.3.11 | GUI framework |
| [lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX) | ^1.2.0 | Display driver (Panel_RGB) |
| [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) | ^6.21.5 | JSON parsing |
| TAMC_GT911 *(local fork)* | — | GT911 touch controller; local copy in `lib/TAMC_GT911/` with ESP32-S3 fixes |

Built-in ESP32 Arduino libraries used: `WiFi`, `WiFiClientSecure`, `HTTPClient`, `Preferences`, `Wire`, `time.h`.

---

## Building & Flashing

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- USB cable connected to the CrowPanel's USB port

### Steps

```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/smart-clock.git
cd smart-clock/SmartClockProject

# Add your API keys to include/secrets.h:
#   BALLDONTLIE_API_KEY  — Ball Don't Lie NFL/NBA API (balldontlie.io)
#   N2YO_API_KEY         — ISS pass predictions       (n2yo.com)
#   RAPIDAPI_JOKE_KEY    — RapidAPI Dad Jokes          (rapidapi.com)

# Full clean build (required on first build or after sdkconfig.defaults changes)
pio run --target clean
pio run --target upload

# Monitor serial output
pio device monitor
```

Or open the `SmartClockProject/` folder in VS Code with the PlatformIO extension and use the **Upload** button.

> **After any change to `sdkconfig.defaults`:** Run `pio run --target clean` before uploading.

### First Boot
1. The device emits a **single short beep** — confirms the buzzer and I2C co-processor are functional
2. The **Setup screen** appears automatically
3. Tap **⟳ Scan** to find nearby networks; tap your network to fill the SSID field
4. Enter your WiFi password and 5-digit US ZIP code
5. Use the **Night Brightness** slider to set your preferred dimmed level (default 50%)
6. Tap **Connect & Save**
7. The clock connects, syncs time, and fetches all data (~30 seconds)
8. The **Clock screen** displays automatically

On all subsequent boots the device auto-connects using saved credentials, and the clock shows the last saved time immediately while connecting.

### Returning to Setup
Tap **⚙ Setup** in the navigation bar at any time.

---

## Project Structure

```
SmartClockProject/
├── platformio.ini           # PlatformIO build config (ESP32-S3, OPI PSRAM, 8 MB flash)
├── sdkconfig.defaults       # ESP-IDF overrides: LCD_RGB_RESTART_IN_VSYNC, SPIRAM_MALLOC_ALWAYSINTERNAL=4096
├── LICENSE
├── README.md
├── .gitignore
│
├── include/
│   ├── config.h             # Pin definitions, screen IDs, update intervals, stock symbols, API base URLs
│   ├── LGFX_Driver.h        # LovyanGFX Panel_RGB driver; use_psram=2, 16 MHz, official Elecrow timing
│   └── lv_conf.h            # LVGL 8.3.11 config (fonts, LV_COLOR_16_SWAP=1, theme settings)
│
├── lib/
│   └── TAMC_GT911/          # Local GT911 touch library fork
│       ├── TAMC_GT911.h
│       └── TAMC_GT911.cpp   # Fixes: no double Wire.begin; reset() writes CONFIG_FRESH=1
│
└── src/
    ├── main.cpp             # setup(), loop(), WiFi, NTP, fetch orchestration, BM8563 RTC sync, 30 s task watchdog
    ├── prefs_mgr.h          # NVS persistence (WiFi, ZIP, city, UTC offset, brightness, stock symbols/names, NBA team IDs)
    ├── rtc_bm8563.h         # BM8563 hardware RTC driver (I2C 0x51, battery-backed, PCF8563-compatible)
    ├── backlight.h          # Software brightness (bswap16-aware RGB565 pixel scaling in disp_flush)
    ├── buzzer.h             # Piezo buzzer via STC8H I2C 0x30 (0xF6=ON, 0xF7=OFF, buzzer_beep())
    ├── weather_api.h        # Zippopotam geocode + Open-Meteo weather + 5-day forecast + UV; persistent TLS client
    ├── moon.h               # Moon phase calculation (local, no network)
    ├── iss_api.h            # ISS visible pass times via N2YO API
    ├── alerts_api.h         # NWS active weather alerts
    ├── news_api.h           # Google News RSS fetch & XML streaming parser (top US headlines)
    ├── stock_api.h          # Yahoo Finance chart API — one HTTPS request per symbol
    ├── nfl_api.h            # Ball Don't Lie NFL games fetch & parser
    ├── nba_api.h            # Ball Don't Lie NBA games fetch & parser (Lakers + Warriors)
    ├── joke_api.h           # RapidAPI Dad Jokes — fetch & parse setup/punchline
    ├── ui_setup.h           # Setup screen: two tabs — (1) WiFi/ZIP/brightness, (2) configurable stocks & NBA teams
    ├── ui_main.h            # Main clock screen (weather + 6-stock market panel + UTC)
    │                        #   also defines _create_nav_bar() shared by all screens
    ├── ui_news.h            # Full news list screen
    ├── ui_stocks.h          # Detailed stock cards screen (3×2 grid: S&P, DOW, VYMI, VYM, Gold, Silver)
    ├── ui_forecast.h        # 5-day forecast screen (cards + UV + ISS strip)
    ├── ui_hourly.h          # 3-day hourly charts (temp, wind, precip) with auto-scroll
    ├── ui_nfl.h             # NFL schedule screen (scrollable game list, 7-day window)
    ├── ui_nba.h             # NBA schedule screen — Lakers & Warriors, 7-day window
    ├── ui_joke.h            # Joke screen — setup panel + punchline panel + staleness indicator
    └── ui_alert.h           # Alert banner on lv_layer_top() (appears above all screens)
```

---

## Configuration

All tunable constants live in [`include/config.h`](include/config.h):

```cpp
// Stock symbols — compile-time defaults (overridden at runtime via Setup → Tab 2 → NVS)
#define STOCK_COUNT  6
static const char* STOCK_SYMBOLS_DEFAULT[STOCK_COUNT] = {
    "^GSPC", "^DJI", "VYMI", "VYM", "GC=F", "SI=F"
};
static const char* STOCK_NAMES_DEFAULT[STOCK_COUNT] = {
    "S&P 500", "DOW JONES", "VYMI", "VYM", "GOLD", "SILVER"
};

// Update frequencies (milliseconds)
#define WEATHER_UPDATE_MS   (60UL * 60 * 1000)       //  1 hour
#define STOCKS_UPDATE_MS    ( 5UL * 60 * 1000)       //  5 minutes
#define NEWS_UPDATE_MS      (30UL * 60 * 1000)       // 30 minutes
#define ALERTS_UPDATE_MS    ( 5UL * 60 * 1000)       //  5 minutes
#define ISS_UPDATE_MS       ( 6UL * 60 * 60 * 1000)  //  6 hours
#define NFL_UPDATE_MS       (60UL * 60 * 1000)       //  1 hour
#define NBA_UPDATE_MS       (60UL * 60 * 1000)       //  1 hour
#define JOKE_UPDATE_MS      ( 3UL * 60 * 60 * 1000)  //  3 hours
#define NTP_SYNC_MS         (60UL * 60 * 1000)       //  1 hour

// News feed locale (in news_api.h)
// Change hl= (language), gl= (country), ceid= to target a different region
#define GOOGLE_NEWS_RSS  "https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en"
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| White/blank screen | Wrong display driver | Ensure `LGFX_Driver.h` uses `Panel_RGB`, not `Panel_ILI9488` |
| Image shifts left/right | Pixel clock or vsync timing wrong | Verify `freq_write = 16000000`; hsync 40/48/40; vsync 8/4/8 in `LGFX_Driver.h` |
| Horizontal shift artifact | Incorrect flush pattern | Verify `disp_flush()` uses per-scanline `startWrite/endWrite` — see [Display Stability Notes](#display-stability-notes) |
| Screen jitters on touch/button | D-cache burst to PSRAM | Confirm per-scanline `startWrite/endWrite` in `disp_flush()`; no persistent `gfx.startWrite()` in `setup()` |
| Screen jitters on startup or download | WiFi/JSON PSRAM contention | Confirm `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` and `static StaticJsonDocument` in all `*_api.h` |
| All SSL connections fail after extended uptime (`HTTP -1` on stocks and alerts) | mbedTLS SRAM heap fragmented after ~300–400 SSL handshakes (~12 h uptime) — cert-parse fragments persist across WiFi cycles | Three-layer recovery in `main.cpp`: (1) `recover_ssl_heap()` proactively cycles WiFi when free SRAM < 70 KB; (2) if all 6 stock symbols fail, one reactive WiFi cycle + retry; (3) if retry still fails AND free SRAM < 65 KB, call `ESP.restart()` — BM8563 RTC preserves time, NVS preserves credentials, device recovers in ~10 s with a clean heap. WiFi cycling alone does NOT defragment SRAM because mbedTLS cert-parse fragments are independent of WiFi/LwIP state. Persistent TLS clients (weather, stocks, alerts) further reduce handshake frequency by reusing the SSL context across fetches |
| Device reboots unexpectedly | HTTP request stalled at TCP layer (server accepts connection but never sends response) | 30-second hardware task watchdog (`esp_task_wdt`) resets the device automatically; fed at the top of every fetch call and inside the stock retry loop |
| Screen flickers on touch | LVGL theme animations enabled | Confirm `LV_THEME_DEFAULT_TRANSITION_TIME 0` and `LV_THEME_DEFAULT_GROW 0` in `lv_conf.h` |
| Wrong colors at night brightness | LV_COLOR_16_SWAP not accounted for | Pixel scaler must call `__builtin_bswap16()` before/after channel extraction — see `backlight.h` |
| No buzzer beep on boot | STC8H not responding | Verify I2C bus initialized before `buzzer_on()`; check serial for I2C errors |
| No buzzer on weather alert | Alert severity not Extreme/Severe | Only "Extreme" and "Severe" NWS severities trigger the buzzer; check `[ALERTS]` serial log |
| Buzzer sounds every 5 min | Same alert re-triggering | Should not occur — alert hash deduplication prevents re-buzzing same alert set |
| Touch not working | GT911 not entering scan mode | Verify `TAMC_GT911::reset()` writes CONFIG_FRESH=1 at end of init |
| Touch coordinates wrong | Wrong rotation | GT911 init uses `ROTATION_INVERTED`; screen is 800×480 |
| Clock shows 1970 on boot | BM8563 battery dead or not found | Check serial for `[RTC] VL flag set` or `BM8563 not found`; replace backup battery |
| Clock shows wrong time on boot | BM8563 never synced or UTC offset changed | Ensure NTP sync completes at least once after setup; check `[RTC] Time written` in serial |
| No weather/forecast data | Bad ZIP or no WiFi | Check serial; verify 5-digit US ZIP code |
| Display doesn't dim at night | Weather data not yet fetched | Auto-dim runs after initial fetch; check `[BRIGHT]` in serial log |
| Stocks show `+0.00%` | `chartPreviousClose` missing | Rare Yahoo Finance API anomaly; will self-correct on next 5-min fetch |
| Stocks show N/A | Yahoo Finance unavailable | Unofficial API; check serial for HTTP error code; retries automatically |
| Gold/Silver show N/A | Futures market closed or API issue | GC=F / SI=F are COMEX futures; unavailable on some weekends |
| Alert banner not appearing | Non-US location or no active alerts | NWS covers US only; 404 treated as "no alerts" (no error shown) |
| ISS strip shows reminder | N2YO API key not set | Add key and re-flash |
| NFL shows setup reminder | BDL API key not set | Add key and re-flash |
| NFL game times wrong | Timezone mismatch | Weather fetch must complete first to set UTC offset |
| NBA shows setup reminder | BDL API key not set | Same key used for both NFL and NBA; add key and re-flash |
| NBA shows no games | Off-season or no upcoming games | NBA regular season runs Oct–Jun; returns empty list in off-season |
| Joke screen shows "Loading joke…" | Key not set, not subscribed, or fetch failed | Add `RAPIDAPI_JOKE_KEY` to `secrets.h`, subscribe to Dad Jokes on RapidAPI, and re-flash; check `[JOKE]` serial log |
| NBA game times wrong | Timezone mismatch | Same as NFL — weather fetch sets UTC offset |
| SRAM alloc failed on boot | Insufficient free internal SRAM | Check serial; `[LVGL] SRAM allocation failed` |
| WiFi scan finds no networks | Mode conflict | `on_scan_networks()` sets `WIFI_STA` before scan; ensure no concurrent mode change |

Enable verbose serial output by opening a monitor at **115200 baud** (`pio device monitor`). Each subsystem prefixes its log lines:

| Prefix | Subsystem |
|---|---|
| `[BOOT]` | Startup sequence |
| `[HW]` | Hardware init (I2C, touch, backlight, buzzer) |
| `[RTC]` | RTC epoch save/restore |
| `[WiFi]` | WiFi connect/reconnect |
| `[NTP]` | Time sync |
| `[WX]` | Weather fetch |
| `[ALERTS]` | NWS alerts fetch + buzzer trigger |
| `[BUZZER]` | Buzzer events |
| `[BRIGHT]` | Auto-dim transitions |
| `[STOCKS]` | Stock data fetch |
| `[NEWS]` | News fetch |
| `[ISS]` | ISS pass time fetch |
| `[NFL]` | NFL schedule fetch |
| `[NBA]` | NBA schedule fetch |
| `[JOKE]` | Joke fetch |
| `[LVGL]` | LVGL init errors |

---

## Known Limitations

- **US ZIP codes only** — Zippopotam geocoding supports US postal codes. International support would require a different geocoding API.
- **Weather alerts: US only** — The NWS API covers only the United States. Non-US coordinates receive a 404 (treated as "no alerts").
- **ISS pass times require N2YO key** — The original Open Notify `iss-pass.json` endpoint was permanently decommissioned in 2023.
- **Stock data: unofficial API** — The Yahoo Finance chart API is an undocumented endpoint. It may occasionally be unavailable. The last successfully fetched price is retained until the next successful update.
- **No SSL certificate pinning** — HTTPS connections use `setInsecure()`. Appropriate for a hobby device displaying public data.
- **Moon phase accuracy** — Accurate to ±1 day; sufficient for display purposes.
- **NFL during off-season** — BDL returns zero games roughly March–August.
- **NBA during off-season** — BDL returns zero games roughly July–September. The screen shows "No Lakers or Warriors games scheduled" gracefully.
- **NBA team filter** — The screen shows all games for your two configured teams (default: Lakers ID 14 and Warriors ID 10), including games against other opponents. A matchup between your two configured teams appears as a single row with a gold accent strip. Change teams anytime via Setup → Tab 2.
- **WiFi scan blocks UI** — `WiFi.scanNetworks()` is synchronous (~2 seconds). Acceptable for one-time setup.
- **Stock fetches block UI briefly** — 6 HTTP requests over a single shared TLS session (HTTP/1.1 keep-alive); only 1 TLS handshake per batch instead of 6. Occurs only every 5 minutes.
- **RTC battery** — The BM8563's backup battery maintains time when the board is unpowered. If the battery is depleted, the VL flag is set and the driver falls back to NTP sync on next WiFi connection.
- **Panel_RGB DMA / jitter** — Three root causes fully diagnosed and fixed: D-cache burst (per-scanline flush), WiFi/LwIP PSRAM contention (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`), and ArduinoJson heap in PSRAM (`static StaticJsonDocument`). See [Display Jitter Troubleshooting](#display-jitter-troubleshooting).
- **Google News RSS locale** — The feed is fixed to `en-US`. To target a different country or language, change the `hl=`, `gl=`, and `ceid=` parameters in `GOOGLE_NEWS_RSS` in `src/news_api.h`.
- **Joke API** — Uses RapidAPI Dad Jokes (free tier). All jokes are two-part (setup + punchline). Requires a free RapidAPI account and subscribing to the Dad Jokes API separately from the key itself.

---

## License

MIT — see [LICENSE](LICENSE).
