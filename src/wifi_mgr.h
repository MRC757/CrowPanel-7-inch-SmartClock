#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// wifi_mgr.h — WiFi connection lifecycle: connect/scan, nothing else.
//
// Reconnect scheduling (the 30 s watchdog) and what to do once reconnected
// (re-fetch stale data) stay in main.cpp's loop() / fetch_mgr.h — this file
// only owns the mechanics of getting associated to an access point.
//
// Split out of main.cpp — see app_state.h for the broader rationale. Pure
// code motion, no behavior change.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "app_state.h"
#include "ui_setup.h"   // WifiNetwork, ui_setup_set_status(), ui_setup_show_scan_results()

// ─── Non-blocking-friendly connect: yields to LVGL every 100 ms while waiting ──
static bool wifi_connect(const char* ssid, const char* pass,
                          unsigned long timeout_ms = 15000) {
    if (strlen(ssid) == 0) return false;

    Serial.printf("[WiFi] Connecting to \"%s\" ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeout_ms) {
        lv_timer_handler();   // keep LVGL alive (spinner animation, etc.)
        delay(100);
    }

    wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (wifi_connected) {
        Serial.printf("[WiFi] Connected, IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Connection timed out");
        WiFi.disconnect();
    }
    return wifi_connected;
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup screen callback — called when user presses "Scan WiFi"
// ─────────────────────────────────────────────────────────────────────────────
static void on_scan_networks() {
    ui_setup_set_status("Scanning for WiFi networks...", lv_color_hex(0xffb300));
    lv_timer_handler();   // render the status update before blocking

    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();   // blocking ~2 s; safe on setup screen

    if (n <= 0) {
        WiFi.scanDelete();
        ui_setup_set_status("No networks found. Try again.", lv_color_hex(0xf44336));
        return;
    }

    // Copy results into the struct array that ui_setup_show_scan_results expects
    int m = (n > 20) ? 20 : n;
    static WifiNetwork nets[20];
    for (int i = 0; i < m; i++) {
        String s = WiFi.SSID(i);
        strncpy(nets[i].ssid, s.c_str(), sizeof(nets[i].ssid) - 1);
        nets[i].ssid[sizeof(nets[i].ssid) - 1] = '\0';
        nets[i].rssi    = WiFi.RSSI(i);
        nets[i].secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    ui_setup_show_scan_results(nets, m);
}
