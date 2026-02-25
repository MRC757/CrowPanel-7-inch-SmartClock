// ─────────────────────────────────────────────────────────────────────────────
// LovyanGFX driver for CrowPanel Advance 7" HMI (ESP32-S3, 800x480 RGB panel)
//
// Uses Panel_RGB + Bus_RGB (parallel 16-bit RGB interface).
// Touch is handled separately via TAMC_GT911 + Wire (see main.cpp).
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
public:
    lgfx::Bus_RGB   _bus_instance;
    lgfx::Panel_RGB _panel_instance;

    LGFX(void) {
        // ── Panel geometry ──────────────────────────────────────────────────
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = 800;
            cfg.memory_height = 480;
            cfg.panel_width   = 800;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel_instance.config(cfg);
        }

        // ── Use PSRAM double-buffer for the internal frame buffer ───────────
        // use_psram = 2: allocates two PSRAM framebuffers and swaps at vsync.
        // The LCD_CAM reads from the front buffer while pushImageDMA writes to
        // the back buffer.  At vsync the pointers swap atomically — eliminating
        // all tearing without requiring waitDMA() in the flush callback.
        {
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 2;
            _panel_instance.config_detail(cfg);
        }

        // ── RGB parallel bus (16-bit) ────────────────────────────────────────
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // Blue channel  (B[4:0])
            cfg.pin_d0  = GPIO_NUM_21;   // B0
            cfg.pin_d1  = GPIO_NUM_47;   // B1
            cfg.pin_d2  = GPIO_NUM_48;   // B2
            cfg.pin_d3  = GPIO_NUM_45;   // B3
            cfg.pin_d4  = GPIO_NUM_38;   // B4
            // Green channel (G[5:0])
            cfg.pin_d5  = GPIO_NUM_9;    // G0
            cfg.pin_d6  = GPIO_NUM_10;   // G1
            cfg.pin_d7  = GPIO_NUM_11;   // G2
            cfg.pin_d8  = GPIO_NUM_12;   // G3
            cfg.pin_d9  = GPIO_NUM_13;   // G4
            cfg.pin_d10 = GPIO_NUM_14;   // G5
            // Red channel   (R[4:0])
            cfg.pin_d11 = GPIO_NUM_7;    // R0
            cfg.pin_d12 = GPIO_NUM_17;   // R1
            cfg.pin_d13 = GPIO_NUM_18;   // R2
            cfg.pin_d14 = GPIO_NUM_3;    // R3
            cfg.pin_d15 = GPIO_NUM_46;   // R4

            // Sync signals
            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync   = GPIO_NUM_41;
            cfg.pin_hsync   = GPIO_NUM_40;
            cfg.pin_pclk    = GPIO_NUM_39;
            cfg.freq_write  = 16000000;  // 16 MHz pixel clock.

            // Timing — CrowPanel Advance 7" (ESP32-S3 IPS panel)
            // Large horizontal blanking (128 clocks/line) gives the LCD DMA extra
            // dead time each scanline to catch up on PSRAM reads after contention.
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch  = 40;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 8;
            cfg.pclk_idle_high    = 1;

            _bus_instance.config(cfg);
        }

        _panel_instance.setBus(&_bus_instance);
        setPanel(&_panel_instance);
    }
};

// Global display instance — shared with main.cpp and UI modules
LGFX gfx;
