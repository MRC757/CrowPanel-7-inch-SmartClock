#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// rtc_bm8563.h — BM8563 (PCF8563-compatible) RTC driver for CrowPanel Advance
//
// The BM8563 sits at I2C address 0x51 on the CrowPanel Advance 7" V1.3 board.
// It keeps time across power cycles via its backup battery.
//
// Requires: Wire must be initialized before calling any function here.
// ─────────────────────────────────────────────────────────────────────────────
#include <Wire.h>
#include <time.h>

#define BM8563_ADDR  0x51

// BCD ↔ decimal helpers
static inline uint8_t bcd2dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static inline uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

// ─────────────────────────────────────────────────────────────────────────────
// Initialize the BM8563 — clear control registers and check if it responds.
// Returns true if the chip ACKs on the bus.
// ─────────────────────────────────────────────────────────────────────────────
inline bool rtc_init() {
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x00);   // Control_Status_1 register
    Wire.write(0x00);   // normal mode, clock running
    Wire.write(0x00);   // Control_Status_2: no alarms/timer
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
        Serial.println("[RTC] BM8563 found at 0x51");
    } else {
        Serial.printf("[RTC] BM8563 not found (I2C err %d)\n", err);
    }
    return err == 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read time from BM8563 into a struct tm.
// Returns true if the read succeeded and the VL (voltage-low) flag is clear
// (meaning the clock data is reliable).
// ─────────────────────────────────────────────────────────────────────────────
inline bool rtc_read(struct tm& t) {
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x02);   // start at Seconds register
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)7) != 7) return false;

    uint8_t sec  = Wire.read();
    uint8_t min  = Wire.read();
    uint8_t hour = Wire.read();
    uint8_t day  = Wire.read();
    uint8_t wday = Wire.read();
    uint8_t mon  = Wire.read();
    uint8_t year = Wire.read();

    // Bit 7 of seconds register = VL (voltage-low) flag.
    // If set, the clock integrity is not guaranteed (battery was dead).
    bool vl = (sec & 0x80);

    t.tm_sec  = bcd2dec(sec & 0x7F);
    t.tm_min  = bcd2dec(min & 0x7F);
    t.tm_hour = bcd2dec(hour & 0x3F);
    t.tm_mday = bcd2dec(day & 0x3F);
    t.tm_wday = bcd2dec(wday & 0x07);
    t.tm_mon  = bcd2dec(mon & 0x1F) - 1;   // struct tm months are 0-based
    t.tm_year = bcd2dec(year) + 100;        // struct tm years since 1900; BM8563 stores 0-99 (2000-2099)
    t.tm_isdst = 0;

    if (vl) {
        Serial.println("[RTC] VL flag set — clock data may be unreliable (battery issue?)");
    }

    return !vl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Write a struct tm to the BM8563.
// The struct tm should contain LOCAL time (not UTC), since we read it back
// as local time and display directly.
// ─────────────────────────────────────────────────────────────────────────────
inline bool rtc_write(const struct tm& t) {
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x02);   // start at Seconds register
    Wire.write(dec2bcd(t.tm_sec)  & 0x7F);        // clear VL flag
    Wire.write(dec2bcd(t.tm_min)  & 0x7F);
    Wire.write(dec2bcd(t.tm_hour) & 0x3F);
    Wire.write(dec2bcd(t.tm_mday) & 0x3F);
    Wire.write(dec2bcd(t.tm_wday) & 0x07);
    Wire.write(dec2bcd(t.tm_mon + 1) & 0x1F);     // BM8563 months are 1-based
    Wire.write(dec2bcd(t.tm_year - 100));          // BM8563 year 0-99 = 2000-2099
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
        Serial.printf("[RTC] Time written: %04d-%02d-%02d %02d:%02d:%02d\n",
                      t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                      t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        Serial.printf("[RTC] Write failed (I2C err %d)\n", err);
    }
    return err == 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: write a time_t (UTC epoch) to the RTC as local time.
// Applies the given UTC offset to convert to local before writing.
// ─────────────────────────────────────────────────────────────────────────────
inline bool rtc_write_epoch(time_t utc_epoch, int utc_offset_sec) {
    time_t local_epoch = utc_epoch + utc_offset_sec;
    struct tm t;
    gmtime_r(&local_epoch, &t);   // treat shifted epoch as "local" via gmtime
    return rtc_write(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: read local time from RTC and set the ESP32 system clock.
// Returns true if the RTC had valid data and the system clock was set.
// ─────────────────────────────────────────────────────────────────────────────
inline bool rtc_restore_system_time(int utc_offset_sec) {
    struct tm t;
    if (!rtc_read(t)) return false;

    // The RTC stores local time.  Convert back to UTC epoch for settimeofday().
    // Use mktime-like math without relying on POSIX TZ (which may not be set yet).
    time_t local_epoch = mktime(&t);          // treats t as UTC → gives "local epoch"
    time_t utc_epoch   = local_epoch - utc_offset_sec;

    timeval tv = { utc_epoch, 0 };
    settimeofday(&tv, nullptr);

    Serial.printf("[RTC] System time restored from BM8563: %04d-%02d-%02d %02d:%02d:%02d (local)\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}
