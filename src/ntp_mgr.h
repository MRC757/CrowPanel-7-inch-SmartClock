#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ntp_mgr.h — NTP time sync, confirmed via an actual server response.
//
// Split out of main.cpp — see app_state.h for the broader rationale. Pure
// code motion, no behavior change.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "app_state.h"
#include "rtc_bm8563.h"   // rtc_write()

// Set by _ntp_sync_cb() the moment SNTP actually applies a server response via
// settimeofday()/adjtime() — see ntp_sync() for why this can't be inferred
// from getLocalTime() (system time is already valid from the BM8563 restore
// at boot, so that check alone can't tell "old RTC time" from "fresh NTP time").
static volatile bool ntp_sync_confirmed = false;
static void _ntp_sync_cb(struct timeval* /*tv*/) { ntp_sync_confirmed = true; }

// ─────────────────────────────────────────────────────────────────────────────
// NTP sync — uses UTC offset stored in g_prefs (or returned by Open-Meteo).
// Calling configTime() with the correct total offset gives local time via
// getLocalTime(), no POSIX TZ string required.
//
// BUG FIXED 2026-09: the wait loop used to poll getLocalTime() and treat any
// success as "synced". That's wrong — rtc_restore_system_time() already calls
// settimeofday() from the BM8563 at boot, before WiFi even connects, so
// getLocalTime() returns true on the very first 200 ms poll using the OLD
// RTC time, almost always before the real NTP UDP round-trip completes. The
// loop then wrote that same unverified time back to the RTC and immediately
// stopped SNTP — so "hourly re-sync" never actually corrected drift, it just
// echoed the RTC's own value back to itself. Symptom: the clock drifts by
// the BM8563 crystal's real tolerance (observed ~2 min) and NTP never fixes
// it. Fix: wait for _ntp_sync_cb() — fired by the SNTP client only when it
// actually applies a server response — instead of inferring success from
// system time merely being non-zero.
//
// Caller must register the callback once, before the first configTime() call:
//   sntp_set_time_sync_notification_cb(_ntp_sync_cb);
// (done once in main.cpp's setup(), before its own first configTime() call).
// ─────────────────────────────────────────────────────────────────────────────
static void ntp_sync() {
    if (!wifi_connected) return;
    ntp_sync_confirmed = false;
    // Pass totalOffset as gmtOffset, 0 daylight — Open-Meteo already folds DST in.
    configTime(g_prefs.utc_offset_sec, 0, NTP_SERVER_1, NTP_SERVER_2);
    Serial.printf("[NTP] Sync requested (UTC%+d)\n", g_prefs.utc_offset_sec / 3600);
    last_ntp_ms = millis();

    // Wait for an actual server response (DNS + UDP round-trip; first lookup
    // can be slow), then write the confirmed-fresh time to the BM8563 RTC.
    bool synced = false;
    for (int attempt = 0; attempt < 25; attempt++) {   // up to ~5 s
        esp_task_wdt_reset();
        delay(200);
        if (ntp_sync_confirmed) { synced = true; break; }
    }

    if (synced) {
        struct tm ti;
        if (rtc_available && getLocalTime(&ti, 100)) {
            rtc_write(ti);   // BM8563 stores local time directly
            Serial.println("[NTP] Sync confirmed by server — RTC updated");
        } else {
            Serial.println("[NTP] Sync confirmed by server");
        }
    } else {
        // No response in time — leave the RTC alone (don't overwrite it with
        // unverified system time) and retry on the next periodic sync.
        Serial.println("[NTP] No server response within 5 s — RTC left unchanged, will retry");
    }

    // configTime() leaves ESP-IDF's SNTP client running a periodic background
    // re-sync (its own lwIP-thread DNS callback) indefinitely. We already
    // manage re-sync ourselves via a periodic ntp_sync() call every
    // NTP_SYNC_MS, so that background client serves no purpose here — and its
    // DNS callback can fire concurrently with our own HTTPS DNS lookups
    // (e.g. the weather geocode fetch immediately following), racing on the
    // lwIP TCP/IP core lock with no protection. Observed as:
    //   "assert failed: udp_new_ip_type ... Required to lock TCPIP core
    //    functionality!" — a hard crash/reboot. Stopping the client here
    //   removes the race entirely.
    if (esp_sntp_enabled()) esp_sntp_stop();
}
