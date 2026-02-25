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
