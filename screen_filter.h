#pragma once
#include <stdint.h>

/*
 * VitaBrightEX Screen Filter System
 * ===================================
 * Rosalina-equivalent display colour pipeline for PS Vita.
 *
 * On 3DS, Rosalina writes a 256-entry per-channel GPU LUT into
 * GPU_FB_TOP_COL_LUT_INDEX/ELEM hardware registers.
 *
 * On PS Vita the analogous mechanisms are:
 *   OLED (Vita 1000) — the panel's SET_NORMAL_GAMMA_CONTROL (0xF9)
 *       LUT, already controlled by VitaBrightEX.  Screen filters are
 *       applied by encoding gamma + CCT + contrast into the 17-row LUT.
 *   LCD  (Vita 2000) — the IFTU CSC (Image Format Transform Unit
 *       Colour Space Conversion) matrix written via SceIftu kernel
 *       functions.  This is a 3x3 fixed-point matrix applied to every
 *       pixel in hardware, before it reaches the LCD panel.
 *
 * Additionally, sceDisplaySetInvertColorsForDriver works on both
 * display types and gives us a free hardware invert mode.
 *
 * Filter parameters mirror Rosalina's for familiarity:
 *   cct        — colour temperature in Kelvin (1000–25100, default 6500)
 *   gamma      — display gamma exponent (0.1–8.0, default 1.0)
 *   contrast   — linear contrast multiplier (0.0–4.0, default 1.0)
 *   brightness — black-level offset (−1.0–1.0, default 0.0)
 *   invert     — invert all colours (bool)
 *   panel_enhance — apply IPS/OLED colour curve correction (0=off,1=IPS,2=VA)
 *
 * For OLED the filter is baked into the 17-row LUT on apply.
 * For LCD  the filter is written to the IFTU CSC matrix.
 */

/* ------------------------------------------------------------------ */
/* Filter state                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t cct;           /* colour temperature K, 1000–25100       */
    float    gamma;         /* gamma exponent                          */
    float    contrast;      /* contrast multiplier                     */
    float    brightness;    /* black-level offset                      */
    int      invert;        /* 0 = normal, 1 = inverted                */
    int      panel_enhance; /* 0 = off, 1 = IPS fix, 2 = sRGB fix     */
} ScreenFilterParams;

/* Active filter parameters (read-only from other modules) */
extern ScreenFilterParams g_screen_filter;

/* ------------------------------------------------------------------ */
/* Colour temperature presets (Kelvin) — same as Rosalina             */
/* ------------------------------------------------------------------ */
#define CCT_DEFAULT         6500
#define CCT_AQUARIUM        10000
#define CCT_OVERCAST_SKY    7500
#define CCT_DAYLIGHT        5500
#define CCT_FLUORESCENT     4200
#define CCT_HALOGEN         3400
#define CCT_INCANDESCENT    2700
#define CCT_WARM_INCAN      2300
#define CCT_CANDLE          1900
#define CCT_EMBER           1200

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/*
 * Load filter settings from the [filter] section of vitabrightex.cfg.
 * Called by config_load() automatically.
 */
void screen_filter_load_config(void);

/*
 * Apply the current g_screen_filter to the appropriate display hardware.
 * For OLED: bakes the filter into lookupNew[] then re-injects the LUT.
 * For LCD:  writes the IFTU CSC matrix and calls SetColorSpaceMode.
 * is_oled: 1 for Vita 1000, 0 for Vita 2000.
 */
void screen_filter_apply(int is_oled);

/*
 * Convenience: set a CCT preset and apply immediately.
 */
void screen_filter_set_cct(uint16_t cct, int is_oled);

/*
 * Reset to neutral (6500K, gamma 1.0, no contrast/brightness, no invert).
 */
void screen_filter_reset(int is_oled);

/*
 * Syscall exports (userland companion app can call these).
 */
int vitabrightFilterGetParams(ScreenFilterParams *out);
int vitabrightFilterSetParams(const ScreenFilterParams *in, int is_oled);
int vitabrightFilterReset(int is_oled);
