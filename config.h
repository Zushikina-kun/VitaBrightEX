#pragma once

/*
 * VitaBrightEX Configuration System
 *
 * Reads vitabrightex.cfg from ur0:/tai/ or ux0:/tai/
 * All settings are optional; defaults are used if missing.
 */

#define CFG_FILE1 "ur0:tai/vitabrightex.cfg"
#define CFG_FILE2 "ux0:tai/vitabrightex.cfg"

/* Maximum line length in config file */
#define CFG_MAX_LINE 128

/*
 * Panel-specific OLED LUT override paths.
 * If panel_lut_override is non-empty, that file is loaded regardless of
 * the auto-detected panel type.
 */
typedef struct {
    /* OLED options */
    int  oled_panel_lut_override;     /* 0 = auto, 1 = use panel_lut_path */
    char panel_lut_path[128];         /* custom LUT path when override=1 */

    /* Color balance tweaks applied on top of the LUT (signed, -127..127) */
    int  color_r_bias;                /* red channel bias   */
    int  color_g_bias;                /* green channel bias */
    int  color_b_bias;                /* blue channel bias  */

    /* Night/warm mode: tint the LUT toward amber at low brightness */
    int  night_mode_enabled;          /* 0 = off, 1 = on */
    int  night_mode_threshold;        /* brightness level (0-16) below which tint activates */

    /* LCD (Vita 2000) options */
    int  lcd_color_space_mode;        /* 0 = limited, 1 = full (OLED emulation) */
    int  lcd_rgb_range_mode;          /* 0 = limited RGB, 1 = full RGB (0-255) */
    int  lcd_saturation_boost;        /* 0 = off, 1 = on  */

    /* IPS-style subpixel sharpening for LCD (Vita 2000) */
    int  lcd_ips_enhance;             /* 0 = off, 1 = on */

    /* Dimming workaround (OLED): prevent screen going bright on auto-dim */
    int  oled_dim_workaround;         /* 1 = enabled (default), 0 = disabled */

    /* ---------------------------------------------------------------- */
    /* Screen filter (Rosalina-equivalent colour pipeline)              */
    /* Applied on top of brightness control for both OLED and LCD.      */
    /* ---------------------------------------------------------------- */
    int   filter_cct;            /* colour temperature K, 1000–25100 (default 6500) */
    float filter_gamma;          /* gamma exponent 0.1–8.0 (default 1.0)            */
    float filter_contrast;       /* contrast multiplier 0.0–4.0 (default 1.0)       */
    float filter_brightness;     /* black-level offset −1.0–1.0 (default 0.0)       */
    int   filter_invert;         /* 0 = normal, 1 = inverted (default 0)            */
    int   filter_panel_enhance;  /* 0 = off, 1 = IPS curve fix, 2 = sRGB fix        */
} VitaBrightConfig;

/* Populated by config_load(); always safe to use after that call. */
extern VitaBrightConfig g_config;

int config_load(void);
