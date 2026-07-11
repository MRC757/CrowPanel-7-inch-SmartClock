#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// buzzer.h — Piezo buzzer control via STC8H1K28 MCU (v1.2 and v1.3 boards)
//
// The STC8H co-processor lives at I2C address 0x30.  It manages both the
// backlight (cmd 0x10) and the buzzer (cmd 0xF6 = on, 0xF7 = off).
// Wire must already be initialized before calling these functions.
// ─────────────────────────────────────────────────────────────────────────────
#include <Wire.h>
#include <lvgl.h>

#define BUZZER_I2C_ADDR  0x30
#define BUZZER_CMD_ON    0xF6   // 246 — activates the buzzer
#define BUZZER_CMD_OFF   0xF7   // 247 — silences the buzzer

inline void buzzer_on() {
    Wire.beginTransmission(BUZZER_I2C_ADDR);
    Wire.write(BUZZER_CMD_ON);
    Wire.endTransmission();
}

inline void buzzer_off() {
    Wire.beginTransmission(BUZZER_I2C_ADDR);
    Wire.write(BUZZER_CMD_OFF);
    Wire.endTransmission();
}

// Blocking beep sequence — keeps LVGL alive during waits so the UI doesn't
// freeze while the alert is sounding.
// count  : number of beeps
// on_ms  : buzzer-on duration per beep (ms)
// off_ms : silence gap between beeps (ms)
static void buzzer_beep(int count, int on_ms, int off_ms) {
    for (int i = 0; i < count; i++) {
        buzzer_on();
        unsigned long t = millis();
        while (millis() - t < (unsigned long)on_ms) {
            lv_timer_handler();
            delay(5);
        }
        buzzer_off();
        if (i < count - 1) {
            t = millis();
            while (millis() - t < (unsigned long)off_ms) {
                lv_timer_handler();
                delay(5);
            }
        }
    }
}

// 5-second pulsing alert — groups rapid on/off pulses into bursts separated by
// a short gap, then repeats until total_ms elapses.  Keeps LVGL alive throughout.
// pulse_on  : buzzer-on time per pulse (ms)
// pulse_off : silence between pulses within a burst (ms)
// burst_len : pulses per burst before the burst gap
// burst_gap : silence between bursts (ms)
// total_ms  : total alert duration (ms)
static void buzzer_alert_pulse(int pulse_on, int pulse_off,
                                int burst_len, int burst_gap,
                                unsigned long total_ms) {
    unsigned long start = millis();
    int pulse_in_burst = 0;

    while (millis() - start < total_ms) {
        // ON phase
        buzzer_on();
        unsigned long t = millis();
        while (millis() - t < (unsigned long)pulse_on) {
            if (millis() - start >= total_ms) { buzzer_off(); return; }
            lv_timer_handler();
            delay(5);
        }
        buzzer_off();
        pulse_in_burst++;

        // Choose gap: burst_gap after a full burst, pulse_off between pulses
        bool burst_done = (pulse_in_burst >= burst_len);
        int gap = burst_done ? burst_gap : pulse_off;
        if (burst_done) pulse_in_burst = 0;

        t = millis();
        while (millis() - t < (unsigned long)gap) {
            if (millis() - start >= total_ms) return;
            lv_timer_handler();
            delay(5);
        }
    }
    buzzer_off();
}
