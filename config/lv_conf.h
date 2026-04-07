/**
 * lv_conf.h — LVGL 9.x configuration for CYD Morse Sender
 *
 * IMPORTANT: This file belongs at:
 *   Documents\Arduino\libraries\lv_conf.h
 * (next to the lvgl\ folder, NOT inside it)
 */

#if 1   /* <-- must be 1, not 0, or the whole file is ignored */

#ifndef LV_CONF_H
#define LV_CONF_H

#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

/*====================
   COLOR SETTINGS
 *====================*/

/* ILI9341 is 16-bit color (RGB565) */
#define LV_COLOR_DEPTH 16

/* LV_COLOR_16_SWAP is an LVGL 8 setting. In LVGL 9, byte order is controlled
   per-display via lv_display_set_color_format(LV_COLOR_FORMAT_RGB565_SWAPPED).
   Do not define LV_COLOR_16_SWAP here. */

/*====================
   MEMORY SETTINGS
 *====================*/

/* Internal LVGL memory pool size in bytes.
   48 KB is comfortable for three screens with labels, buttons, keyboard. */
#define LV_MEM_SIZE (48U * 1024U)

/* Use the built-in allocator (no external heap manager needed) */
#define LV_MEM_CUSTOM 0

/*====================
   HAL SETTINGS
 *====================*/

/* Millisecond tick — provided by Arduino millis(), no separate task needed */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE       <Arduino.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/* Default display refresh period (ms).  30 ms ≈ 33 fps — good for CYD.
   NOTE: renamed from LV_DISP_DEF_REFR_PERIOD in LVGL 8 */
#define LV_DEF_REFR_PERIOD 30

/* Input device read period (ms) */
#define LV_INDEV_DEF_READ_PERIOD 30

/*====================
   FEATURES
 *====================*/

/* Enable the animation engine (used by screen transitions) */
#define LV_USE_ANIM 1           /* was LV_USE_ANIMATION in v8 */

/* Enable complex drawing (shadows, gradients — needed by default theme) */
#define LV_DRAW_COMPLEX 1

/*====================
   FONT SETTINGS
 *====================*/

/* Enable the Montserrat fonts used by SquareLine-generated screens */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGET ENABLES
   Enable only what the project uses to save flash/RAM.
 *====================*/

/* All standard widgets enabled — ui_helpers.c (SquareLine boilerplate)
   references every widget type regardless of which screens use them. */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1
#define LV_USE_KEYBOARD   1

/*====================
   THEME
 *====================*/

#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK            1
    #define LV_THEME_DEFAULT_GROW            0
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/*====================
   LOGGING  (disable in production to save UART bandwidth)
 *====================*/

#define LV_USE_LOG 0
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

/*====================
   ASSERT / DEBUG
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

#endif /* LV_CONF_H */
#endif /* if 1 */
