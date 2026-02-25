#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// backlight.h — Software brightness control (0–100 %)
//
// The CrowPanel 7" backlight is driven by the TCA9534 I/O expander which
// provides digital ON/OFF only (no hardware PWM).  Brightness dimming is
// therefore achieved by scaling each RGB565 pixel in the LVGL disp_flush
// callback before pushing to the display.
//
// Usage (in disp_flush):
//   backlight_scale_buf((uint16_t*)px_map, pixel_count);
//   gfx.pushImage(...);
//   lv_disp_flush_ready(disp);
// ─────────────────────────────────────────────────────────────────────────────
#include <stdint.h>

static uint8_t g_brightness_pct = 100;  // 0–100 (100 = full brightness)

inline void backlight_set_pct(uint8_t pct) {
    if (pct > 100) pct = 100;
    g_brightness_pct = pct;
}

inline uint8_t backlight_get_pct() { return g_brightness_pct; }

// ─── Scale an entire RGB565 buffer in-place ────────────────────────────────
// Called every disp_flush before gfx.pushImage().
// The early-return for 100% makes the daytime (full-brightness) path free.
// Safe to modify in-place: LVGL hands off the buffer until flush_ready().
static inline void backlight_scale_buf(uint16_t* buf, int pixel_count) {
    const uint8_t pct = g_brightness_pct;
    if (pct >= 100) return;

    for (int i = 0; i < pixel_count; i++) {
        // LV_COLOR_16_SWAP=1: LVGL stores RGB565 with its two bytes swapped.
        // Un-swap first so channel extraction uses the standard bit layout,
        // then re-swap after packing so pushImageDMA still gets the expected format.
        const uint16_t px = __builtin_bswap16(buf[i]);
        uint32_t r = (px >> 11) & 0x1F;
        uint32_t g = (px >> 5)  & 0x3F;
        uint32_t b =  px        & 0x1F;
        // Integer multiply + round (+ 50 before /100 gives correct rounding)
        r = (r * pct + 50) / 100;
        g = (g * pct + 50) / 100;
        b = (b * pct + 50) / 100;
        buf[i] = __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | b));
    }
}
