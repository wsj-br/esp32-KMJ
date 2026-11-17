#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 140

#define LV_USE_OS LV_OS_NONE
#define LV_DRAW_SW_DRAW_UNIT_CNT 1

#ifdef ESP_PLATFORM
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <esp32-hal-psram.h>
#define LV_MEM_CUSTOM_ALLOC ps_malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC ps_realloc
#else
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (256 * 1024)
#endif

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_DEFAULT &font_chakra_16
#define LV_FONT_CUSTOM_DECLARE \
    extern const lv_font_t font_chakra_16; \
    extern const lv_font_t font_chakra_20; \
    extern const lv_font_t font_plex_14;

#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_LINE 1
#define LV_USE_SLIDER 1
#define LV_USE_TEXTAREA 1
#define LV_USE_SPINBOX 0
#define LV_USE_KEYBOARD 0
#define LV_USE_DEMO_WIDGETS 0

#define LV_TXT_ENC LV_TXT_ENC_UTF8

#endif
