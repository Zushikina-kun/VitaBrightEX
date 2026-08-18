/*
 * VitaBrightEX Screen Filter — Rosalina-equivalent colour pipeline
 *
 * OLED: bakes CCT+gamma+contrast+brightness into the 17-row panel LUT.
 * LCD:  writes a 3×3 IFTU CSC matrix for per-channel colour correction.
 *
 * Fixed-point arithmetic throughout (no libm available in kernel).
 */

#include "screen_filter.h"
#include "config.h"
#include "log.h"
#include "oled/hooks.h"
#include "oled/lut.h"
#include "main.h"
#include "taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

/* ------------------------------------------------------------------ */
/* NIDs                                                                */
/* ------------------------------------------------------------------ */
#define NID_DISPLAY_INVERT_COLORS   0x19140ACD
#define NID_DISPLAY_SET_COLOR_SPACE 0x8D79D187
#define NID_IFTU_SET_CSC_PARAMS     0x0FCBF457

/* ------------------------------------------------------------------ */
/* SceIftu CSC params (from SceDisplay wiki)                           */
/* Matrix is 10.2 signed fixed-point: 0x200 = 1.0                     */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t unk00;
    uint32_t unk04;
    uint32_t unk08;   /* typically 0x3FF */
    uint32_t unk0C;
    uint32_t unk10;   /* typically 0x3FF */
    uint32_t unk14;
    int32_t  csc_rr, csc_rg, csc_rb;
    int32_t  csc_gr, csc_gg, csc_gb;
    int32_t  csc_br, csc_bg, csc_bb;
} SceIftuCscParams;

static const SceIftuCscParams CSC_IDENTITY = {
    0, 0, 0x3FF, 0, 0x3FF, 0,
    0x200, 0, 0,
    0, 0x200, 0,
    0, 0, 0x200
};

/* ------------------------------------------------------------------ */
/* Global filter state                                                 */
/* ------------------------------------------------------------------ */

ScreenFilterParams g_screen_filter = {
    .cct          = CCT_DEFAULT,
    .gamma        = 1.0f,
    .contrast     = 1.0f,
    .brightness   = 0.0f,
    .invert       = 0,
    .panel_enhance = 0,
};

static int (*ksceDisplaySetInvertColors)(int head, int enable)       = NULL;
static int (*ksceDisplaySetColorSpaceMode)(int head, uint32_t mode)  = NULL;
static int (*ksceIftuSetCscParams)(int head, int fb_idx,
                                    const SceIftuCscParams *p)       = NULL;

static int g_filter_funcs_resolved = 0;

/* ------------------------------------------------------------------ */
/* Fixed-point helpers  (16.16 unless noted)                          */
/* ------------------------------------------------------------------ */

#define FP_ONE   (1 << 16)

static int32_t fp_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * b) >> 16);
}

static int32_t float_to_fp(float f) {
    return (int32_t)(f * 65536.0f);
}

/* ------------------------------------------------------------------ */
/* Colour temperature → white point                                   */
/* Planckian locus polynomial approximation (Kang et al. 2002)        */
/* wp[] returned as 16.16 in [0, FP_ONE]                              */
/* ------------------------------------------------------------------ */

static void cct_to_white_point(uint16_t cct, int32_t wp[3]) {
    if (cct < 1000)  cct = 1000;
    if (cct > 25100) cct = 25100;

    int32_t t = (int32_t)cct;
    int32_t r, g, b;

    /* Red */
    if (t <= 6600) {
        r = FP_ONE;
    } else {
        int32_t dt = t - 6600;
        r = FP_ONE - (dt * 2229) / 100000;
        if (r < 0) r = 0;
    }

    /* Green */
    if (t <= 6600) {
        int32_t dt = 6600 - t;
        g = float_to_fp(0.73f) + (dt * 2949) / 100000;
        if (g > FP_ONE) g = FP_ONE;
    } else {
        int32_t dt = t - 6600;
        g = FP_ONE - (dt * 786) / 100000;
        if (g < 0) g = 0;
    }

    /* Blue */
    if (t >= 6500) {
        b = FP_ONE;
    } else if (t <= 1900) {
        b = 0;
    } else {
        b = ((t - 1900) * FP_ONE) / 4600;
        if (b < 0)       b = 0;
        if (b > FP_ONE)  b = FP_ONE;
    }

    wp[0] = r;
    wp[1] = g;
    wp[2] = b;
}

/* ------------------------------------------------------------------ */
/* IPS linearisation table for Vita 2000 LCD (33-point, 8-step LUT)  */
/* ------------------------------------------------------------------ */

static const uint8_t ips_lut_vita2000[33] = {
      0,   6,  13,  21,  31,  42,  54,  67,
     81,  96, 111, 127, 143, 158, 172, 185,
    197, 208, 218, 227, 236, 243, 249, 252,
    253, 254, 254, 255, 255, 255, 255, 255,
    255
};

static uint8_t ips_lookup(uint8_t input) {
    int idx  = input >> 3;
    int frac = input & 0x7;
    if (idx >= 32) return 255;
    uint8_t lo = ips_lut_vita2000[idx];
    uint8_t hi = ips_lut_vita2000[idx + 1];
    return (uint8_t)(lo + ((int)(hi - lo) * frac + 4) / 8);
}

/* ------------------------------------------------------------------ */
/* Resolve display/IFTU functions                                     */
/* ------------------------------------------------------------------ */

static void resolve_display_funcs(void) {
    if (g_filter_funcs_resolved) return;

    module_get_export_func(KERNEL_PID, "SceDisplay", TAI_ANY_LIBRARY,
        NID_DISPLAY_INVERT_COLORS, (uintptr_t *)&ksceDisplaySetInvertColors);

    module_get_export_func(KERNEL_PID, "SceDisplay", TAI_ANY_LIBRARY,
        NID_DISPLAY_SET_COLOR_SPACE, (uintptr_t *)&ksceDisplaySetColorSpaceMode);

    /* SceIftu lives inside SceLowio */
    module_get_export_func(KERNEL_PID, "SceIftu", TAI_ANY_LIBRARY,
        NID_IFTU_SET_CSC_PARAMS, (uintptr_t *)&ksceIftuSetCscParams);
    if (ksceIftuSetCscParams == NULL) {
        /* Try SceLowio as the module name on some firmware builds */
        module_get_export_func(KERNEL_PID, "SceLowio", TAI_ANY_LIBRARY,
            NID_IFTU_SET_CSC_PARAMS, (uintptr_t *)&ksceIftuSetCscParams);
    }

    LOG("[FILT] InvertColors=%p ColorSpace=%p IftuCsc=%p\n",
        ksceDisplaySetInvertColors, ksceDisplaySetColorSpaceMode, ksceIftuSetCscParams);

    g_filter_funcs_resolved = 1;
}

/* ------------------------------------------------------------------ */
/* LCD filter application via IFTU CSC matrix                         */
/* ------------------------------------------------------------------ */

static void apply_filter_to_lcd(void) {
    const ScreenFilterParams *f = &g_screen_filter;
    resolve_display_funcs();

    int32_t wp[3];
    cct_to_white_point(f->cct, wp);

    int32_t fp_contrast = float_to_fp(f->contrast);
    int32_t fp_bright   = float_to_fp(f->brightness);

    /* Diagonal CSC coefficients in IFTU 10.2 format (0x200 = 1.0) */
    int32_t cr = (fp_mul(fp_contrast, wp[0]) * 0x200) >> 16;
    int32_t cg = (fp_mul(fp_contrast, wp[1]) * 0x200) >> 16;
    int32_t cb = (fp_mul(fp_contrast, wp[2]) * 0x200) >> 16;

    /* Brightness: express as an offset to the black level.
     * IFTU unk00/unk04 fields are the input offset (10-bit, sign TBD).
     * A positive brightness value raises the black level.
     * Scale by 0x1FF so brightness=±1.0 maps exactly to the ±0x1FF clamp.
     * (Using 0x3FF would clip the top half of the range and double the
     * effect in the bottom half.) */
    int32_t bright_offset = fp_mul(fp_bright, float_to_fp((float)0x1FF)) >> 16;

    if (f->panel_enhance > 0) {
        uint8_t ref_out = ips_lookup(128);
        int32_t scale = (((int32_t)ref_out) << 16) / 128;
        cr = (cr * (scale >> 8)) >> 8;
        cg = (cg * (scale >> 8)) >> 8;
        cb = (cb * (scale >> 8)) >> 8;
        LOG("[FILT] IPS scale=%d/65536\n", scale);
    }

    /* Clamp coefficients */
    if (cr < 0) cr = 0;
    if (cr > 0x3FF) cr = 0x3FF;
    if (cg < 0) cg = 0;
    if (cg > 0x3FF) cg = 0x3FF;
    if (cb < 0) cb = 0;
    if (cb > 0x3FF) cb = 0x3FF;
    if (bright_offset < -(int32_t)0x1FF) bright_offset = -(int32_t)0x1FF;
    if (bright_offset >  (int32_t)0x1FF) bright_offset =  (int32_t)0x1FF;

    LOG("[FILT] LCD CSC rr=0x%03X gg=0x%03X bb=0x%03X bias=%d\n",
        cr, cg, cb, bright_offset);

    SceIftuCscParams csc = CSC_IDENTITY;
    csc.unk00  = (uint32_t)bright_offset;
    csc.csc_rr = cr;
    csc.csc_gg = cg;
    csc.csc_bb = cb;

    if (ksceIftuSetCscParams != NULL) {
        ksceIftuSetCscParams(0, 0, &csc);
        ksceIftuSetCscParams(0, 1, &csc);
        LOG("[FILT] IFTU CSC written\n");
    } else if (ksceDisplaySetColorSpaceMode != NULL) {
        uint32_t mode = (f->cct < 6000) ? 1 : 0;
        ksceDisplaySetColorSpaceMode(0, mode);
        LOG("[FILT] Fallback SetColorSpaceMode(%u)\n", mode);
    }

    if (ksceDisplaySetInvertColors != NULL)
        ksceDisplaySetInvertColors(0, f->invert ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* Check if filter is at neutral defaults                             */
/* ------------------------------------------------------------------ */

static int filter_is_identity(void) {
    return g_screen_filter.cct == CCT_DEFAULT
        && g_screen_filter.gamma == 1.0f
        && g_screen_filter.contrast == 1.0f
        && g_screen_filter.brightness == 0.0f
        && !g_screen_filter.invert
        && g_screen_filter.panel_enhance == 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void screen_filter_load_config(void) {
    int cct = g_config.filter_cct;
    if (cct < 1000)  cct = 1000;
    if (cct > 25100) cct = 25100;
    g_screen_filter.cct = (uint16_t)cct;

    float gam = g_config.filter_gamma;
    if (gam < 0.1f) gam = 0.1f;
    if (gam > 8.0f) gam = 8.0f;
    g_screen_filter.gamma = gam;

    float con = g_config.filter_contrast;
    if (con < 0.0f) con = 0.0f;
    if (con > 4.0f) con = 4.0f;
    g_screen_filter.contrast = con;

    float bri = g_config.filter_brightness;
    if (bri < -1.0f) bri = -1.0f;
    if (bri >  1.0f) bri =  1.0f;
    g_screen_filter.brightness = bri;

    g_screen_filter.invert        = g_config.filter_invert ? 1 : 0;
    g_screen_filter.panel_enhance = g_config.filter_panel_enhance;
}

void screen_filter_apply(int is_oled) {
    resolve_display_funcs();

    if (is_oled) {
        /*
         * OLED architecture note:
         * The OLED panel's gamma LUT (SET_NORMAL_GAMMA_CONTROL) contains
         * panel-specific calibration values loaded from the per-panel LUT
         * file and post-processed by white-point normalisation. These bytes
         * are panel control registers — NOT linear RGB pixel values.
         * Applying a CCT colour matrix or gamma curve to these bytes via
         * apply_filter_to_oled_lut() produces wrong results because the
         * byte semantics don't map linearly to colour channels.
         *
         * The only safe filter operations on OLED are:
         *   1. Hardware colour invert via sceDisplaySetInvertColorsForDriver
         *
         * CCT / gamma / contrast / brightness are intentionally NOT applied
         * to OLED. The panel's calibrated LUT already looks correct.
         * The vitabright LUT file is the right place to tune OLED colours.
         */
        if (ksceDisplaySetInvertColors != NULL)
            ksceDisplaySetInvertColors(0, g_screen_filter.invert ? 1 : 0);
        return;
    }

    /* LCD path — full filter via IFTU CSC matrix */
    if (filter_is_identity()) {
        if (ksceIftuSetCscParams != NULL) {
            ksceIftuSetCscParams(0, 0, &CSC_IDENTITY);
            ksceIftuSetCscParams(0, 1, &CSC_IDENTITY);
        }
        if (ksceDisplaySetInvertColors != NULL)
            ksceDisplaySetInvertColors(0, 0);
        return;
    }

    apply_filter_to_lcd();
}

void screen_filter_set_cct(uint16_t cct, int is_oled) {
    g_screen_filter.cct = cct;
    screen_filter_apply(is_oled);
}

void screen_filter_reset(int is_oled) {
    g_screen_filter.cct          = CCT_DEFAULT;
    g_screen_filter.gamma        = 1.0f;
    g_screen_filter.contrast     = 1.0f;
    g_screen_filter.brightness   = 0.0f;
    g_screen_filter.invert       = 0;
    g_screen_filter.panel_enhance = 0;
    /* For OLED: only clear invert — don't touch the LUT */
    if (is_oled) {
        if (ksceDisplaySetInvertColors != NULL)
            ksceDisplaySetInvertColors(0, 0);
        return;
    }
    /* For LCD: restore identity CSC */
    resolve_display_funcs();
    if (ksceIftuSetCscParams != NULL) {
        ksceIftuSetCscParams(0, 0, &CSC_IDENTITY);
        ksceIftuSetCscParams(0, 1, &CSC_IDENTITY);
    }
    if (ksceDisplaySetInvertColors != NULL)
        ksceDisplaySetInvertColors(0, 0);
}

/* ------------------------------------------------------------------ */
/* Syscall exports                                                     */
/* L-5 fix: use kernel-side g_is_oled instead of trusting userland   */
/* ------------------------------------------------------------------ */

int vitabrightFilterGetParams(ScreenFilterParams *out) {
    int state;
    ENTER_SYSCALL(state);
    ksceKernelMemcpyKernelToUser((void *)out, &g_screen_filter,
                                 sizeof(ScreenFilterParams));
    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightFilterSetParams(const ScreenFilterParams *in, int is_oled_unused) {
    int state;
    ENTER_SYSCALL(state);
    (void)is_oled_unused;  /* L-5: ignore userland-supplied is_oled */
    ksceKernelMemcpyUserToKernel(&g_screen_filter, (const void *)in,
                                 sizeof(ScreenFilterParams));
    screen_filter_apply(g_is_oled);   /* use kernel-authoritative value */
    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightFilterReset(int is_oled_unused) {
    int state;
    ENTER_SYSCALL(state);
    (void)is_oled_unused;
    screen_filter_reset(g_is_oled);
    EXIT_SYSCALL(state);
    return 0;
}
