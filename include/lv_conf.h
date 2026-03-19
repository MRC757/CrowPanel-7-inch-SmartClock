/**
 * lv_conf.h — LVGL 8.3.11 configuration for Smart Clock
 * Place this file in the include/ directory and build with -DLV_CONF_INCLUDE_SIMPLE
 */

#if 1  /* Set to 1 to enable */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 ====================*/
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1   /* 1 = required: pushImageDMA does raw byte copy; LCD_CAM reads big-endian */
#define LV_COLOR_SCREEN_TRANSP 0
#define LV_COLOR_MIX_ROUND_OFS 0
#define LV_COLOR_CHROMA_KEY lv_color_hex(0x00ff00)

/*=========================
   MEMORY SETTINGS
 =========================*/
/* Use system malloc (goes to PSRAM on ESP32 when PSRAM is enabled) */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 0
    #define LV_MEM_SIZE (64U * 1024U)
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_MEM_BUF_MAX_NUM 16
#define LV_MEMCPY_MEMSET_STD 1

/*====================
   HAL SETTINGS
 ====================*/
#define LV_DISP_DEF_REFR_PERIOD  30   /* ms */
#define LV_INDEV_DEF_READ_PERIOD 30   /* ms */

/* Use millis() as the LVGL tick source — no lv_tick_inc() calls needed */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE       "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DPI_DEF 130

/*========================
   RENDERING SETTINGS
 ========================*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0
#define LV_DITHER_GRADIENT 0
#define LV_DISP_ROT_MAX_BUF (10*1024)

/*=================
   GPU
 =================*/
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

/*================
   LOGGING
 ================*/
#define LV_USE_LOG 0
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
    #define LV_LOG_USE_TIMESTAMP 1
    #define LV_LOG_USE_FILE_LINE 1
#endif

/*================
   ASSERT
 ================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE <stdint.h>
#define LV_ASSERT_HANDLER while(1);

/*==================
   DEBUG / PROFILING
 ==================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0
#define LV_USE_REFR_DEBUG   0

/*==================
   SPRINTF
 ==================*/
#define LV_SPRINTF_CUSTOM 0
#define LV_SPRINTF_USE_FLOAT 1

/*===================
   BUILT-IN FONTS
 ===================*/
/* Enable Montserrat sizes used by the Smart Clock UI */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 1

/* Unscii (code/terminal style) */
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_CUSTOM_DECLARE   /* Place custom font declarations here */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_FONT_FMT_TXT_LARGE 0
/* Sub-pixel rendering is designed for desktop LCDs with a known RGB sub-pixel
   layout.  On the CrowPanel IPS panel it produces coloured fringing on text
   edges that reads as blurriness.  Disable it for clean, sharp glyphs. */
#define LV_USE_FONT_SUBPX 0
#define LV_FONT_SUBPX_BGR 0

/*=========================
   WIDGET ENABLE / DISABLE
 =========================*/
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#   define LV_LABEL_TEXT_SELECTION  1
#   define LV_LABEL_LONG_TXT_HINT   1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#   define LV_ROLLER_INF_PAGES 7
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#   define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#define LV_USE_TABLE      1
#define LV_USE_KEYBOARD   1   /* On-screen keyboard for WiFi/ZIP entry */
#define LV_USE_LIST       1   /* News headline list */
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       0
#define LV_USE_SPINNER    1   /* Loading animation */
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_CHART      1
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_LED        0

/*========================
   THEMES
 ========================*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK 1        /* 1 = dark theme (better for clock) */
    #define LV_THEME_DEFAULT_GROW 0        /* disable grow-on-press (causes repeated redraws) */
    #define LV_THEME_DEFAULT_TRANSITION_TIME 0  /* instant style changes — no animation frames */
#endif
#define LV_USE_THEME_SIMPLE   1
#define LV_USE_THEME_MONO     0

/*============================
   LAYOUT (Flex & Grid)
 ============================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*=====================
   3RD PARTY LIBRARIES
 =====================*/
#define LV_USE_FS_STDIO  0
#define LV_USE_FS_POSIX  0
#define LV_USE_FS_WIN32  0
#define LV_USE_FS_FATFS  0
#define LV_USE_PNG       0
#define LV_USE_BMP       0
#define LV_USE_SJPG      0
#define LV_USE_GIF       0
#define LV_USE_QRCODE    0
#define LV_USE_FREETYPE  0
#define LV_USE_RLOTTIE   0
#define LV_USE_FFMPEG    0

/*==================
   OTHERS
 ==================*/
#define LV_USE_SNAPSHOT  0
#define LV_USE_MONKEY    0
#define LV_USE_GRIDNAV   0
#define LV_USE_FRAGMENT  0
#define LV_USE_IMGFONT   0
#define LV_USE_MSG       0
#define LV_USE_IME_PINYIN 0

/*===================
   EXAMPLES
 ===================*/
#define LV_BUILD_EXAMPLES 0

#endif  /* LV_CONF_H */
#endif  /* "Content enable" */
