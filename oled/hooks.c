#include "hooks.h"
#include "../main.h"
#include "../log.h"
#include "../config.h"
#include "../screen_filter.h"
#include "../taihen_extra.h"
#include "lut.h"
#include "parser.h"
#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <taihen.h>

/* ------------------------------------------------------------------ */
/* NID constants                                                       */
/* ------------------------------------------------------------------ */
#define NID_OLED_GET_BRIGHTNESS    0x43EF811A
#define NID_OLED_SET_BRIGHTNESS    0xF9624C47
#define NID_OLED_GET_DDB           0xC9D5987C
#define NID_OLED_SET_COLOR_SPACE   0xDABBD9D3
#define NID_POWER_SET_MAX_BRIGHT   0x77027B6B

/* ------------------------------------------------------------------ */
/* LUT offsets per panel (segment 0 of SceOled.skprx)                 */
/* ------------------------------------------------------------------ */
#define OLED_LUT_OFF_P4      0x1AB8
#define OLED_LUT_OFF_P5      0x1C20
#define OLED_LUT_OFF_DEFAULT 0x1E00

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static SceUID power_set_max_brightness_hook = -1;
static tai_hook_ref_t power_set_max_brightness_ref = 0;

/*
 * lookupBase — post-load, post-normalise, pre-filter snapshot.
 *              screen_filter uses this as the input so repeated calls
 *              don't compound (fixes C-4).
 * lookupNew  — current injected state (modified by screen_filter).
 */
unsigned char lookupBase[LUT_SIZE];
unsigned char lookupNew[LUT_SIZE];

static SceUID lut_inject = -1;
static SceUID oled_set_brightness_hook = -1;
static tai_hook_ref_t oled_set_brightness_ref = -1;

/* Resolved function pointers */
int (*ksceOledGetBrightness)(void)                                              = NULL;
int (*ksceOledSetBrightness)(unsigned int brightness)                          = NULL;
int (*ksceOledGetDDB)(uint16_t *supplier_id, uint16_t *supplier_elective_data) = NULL;
int (*ksceOledSetDisplayColorSpaceMode)(int mode)                              = NULL;

static int g_panel_type  = OLED_PANEL_UNKNOWN;
static int g_hooks_active = 0;
static int isDimmingWorkAround = 1;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static unsigned char clamp_u8(int v) {
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (unsigned char)v;
}

/* Kernel memcpy — avoids pulling in libc */
static void lut_memcpy(unsigned char *dst, const unsigned char *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

/* ------------------------------------------------------------------ */
/* LUT post-processing (applied to lookupNew during enable)            */
/* ------------------------------------------------------------------ */

static void apply_color_bias(unsigned char lut[LUT_SIZE]) {
    if (g_config.color_r_bias == 0 && g_config.color_g_bias == 0 &&
        g_config.color_b_bias == 0)
        return;

    for (int row = 0; row < LUT_ROWS; row++) {
        unsigned char *r = &lut[row * LUT_LINE_SIZE];
        r[0] = clamp_u8((int)r[0] + g_config.color_r_bias);
        r[1] = clamp_u8((int)r[1] + g_config.color_r_bias);
        r[2] = clamp_u8((int)r[2] + g_config.color_b_bias);
        r[3] = clamp_u8((int)r[3] + g_config.color_g_bias);
        r[4] = clamp_u8((int)r[4] + g_config.color_g_bias);
        r[5] = clamp_u8((int)r[5] + g_config.color_g_bias);
    }
}

static void apply_night_mode(unsigned char lut[LUT_SIZE]) {
    if (!g_config.night_mode_enabled) return;

    int threshold = g_config.night_mode_threshold;
    if (threshold < 0)           threshold = 0;
    if (threshold > LUT_ROWS - 1) threshold = LUT_ROWS - 1;

    for (int row = threshold; row < LUT_ROWS - 1; row++) {
        unsigned char *r = &lut[row * LUT_LINE_SIZE];
        int depth   = row - threshold + 1;
        r[0] = clamp_u8((int)r[0] + depth * 3);
        r[1] = clamp_u8((int)r[1] + depth * 3);
        r[2] = clamp_u8((int)r[2] - depth * 5);
    }
}

/*
 * White-point normalisation (fixes C-5's side-effect concern and H-3).
 * Guards ref[0], ref[2], AND ref[3] for zero before dividing.
 */
static void normalise_white_point(unsigned char lut[LUT_SIZE]) {
    if (g_config.color_r_bias != 0 || g_config.color_g_bias != 0 ||
        g_config.color_b_bias != 0)
        return;

    const int ref_row = 5;
    unsigned char *ref = &lut[ref_row * LUT_LINE_SIZE];

    /* H-3 fix: guard ref[3] == 0 as well as ref[0] and ref[2] */
    if (ref[0] == 0 || ref[2] == 0 || ref[3] == 0) return;

    for (int row = ref_row + 1; row < LUT_ROWS - 1; row++) {
        unsigned char *r = &lut[row * LUT_LINE_SIZE];

        int ref_g = (int)ref[3];
        int cur_g = (int)r[3] + 1;   /* +1 avoids div/0 for degenerate rows */

        int expected_r = ((int)ref[0] * cur_g) / ref_g;
        int expected_b = ((int)ref[2] * cur_g) / ref_g;

        if ((int)r[0] - expected_r > 4) r[0] = clamp_u8(expected_r + 4);
        if ((int)r[1] - expected_r > 4) r[1] = clamp_u8(expected_r + 4);
        if ((int)r[2] - expected_b > 4) r[2] = clamp_u8(expected_b + 4);
    }
}

/* ------------------------------------------------------------------ */
/* Panel detection                                                     */
/* ------------------------------------------------------------------ */

int oled_detect_panel(void) {
    if (ksceOledGetDDB == NULL) return OLED_PANEL_UNKNOWN;

    uint16_t supplier_id = 0, sed = 0;
    int ret = ksceOledGetDDB(&supplier_id, &sed);
    if (ret < 0) {
        LOG("[OLED] GetDDB failed: 0x%08X\n", ret);
        return OLED_PANEL_UNKNOWN;
    }
    LOG("[OLED] DDB supplier_id=0x%04X sed=0x%04X\n", supplier_id, sed);

    switch (sed & 0xFF) {
    case 4:  return OLED_PANEL_4;
    case 5:  return OLED_PANEL_5;
    case 6:  return OLED_PANEL_6;
    default: return OLED_PANEL_UNKNOWN;
    }
}

static uint32_t panel_to_lut_offset(int panel_type) {
    switch (panel_type) {
    case OLED_PANEL_4: return OLED_LUT_OFF_P4;
    case OLED_PANEL_5: return OLED_LUT_OFF_P5;
    default:           return OLED_LUT_OFF_DEFAULT;
    }
}

/* ------------------------------------------------------------------ */
/* Hooks                                                               */
/* ------------------------------------------------------------------ */

int hook_ksceOledSetBrightness(unsigned int brightness) {
    /* C-5 fix: guard against NULL after disable (race window) */
    if (ksceOledGetBrightness == NULL)
        return TAI_CONTINUE(int, oled_set_brightness_ref, brightness);

    if (brightness == 1 && isDimmingWorkAround && g_config.oled_dim_workaround) {
        int old_level = ksceOledGetBrightness();
        if (old_level > 4 * 0x1000)
            return TAI_CONTINUE(int, oled_set_brightness_ref, brightness);
        return TAI_CONTINUE(int, oled_set_brightness_ref, old_level);
    }
    return TAI_CONTINUE(int, oled_set_brightness_ref, brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForOled(int limit) {
    if (power_set_max_brightness_ref == 0) return 0;
    if (limit < 0x10000 && limit >= 0)
        limit = 0x10000 - 2 * 0x1000;
    else
        limit = 0x10000;
    return TAI_CONTINUE(int, power_set_max_brightness_ref, limit);
}

/* ------------------------------------------------------------------ */
/* Internal LUT re-inject (used by screen_filter after modifying      */
/* lookupNew — does NOT reload from disk)                              */
/* ------------------------------------------------------------------ */

int oled_reinject_lut(void) {
    if (!g_hooks_active || lut_inject < 0) return -1;

    /* Release old injection, inject updated lookupNew */
    taiInjectReleaseForKernel(lut_inject);
    lut_inject = -1;

    tai_module_info_t info;
    info.size = sizeof(tai_module_info_t);
    if (taiGetModuleInfoForKernel(KERNEL_PID, "SceOled", &info) < 0) return -2;

    uint32_t lut_off = panel_to_lut_offset(g_panel_type);
    lut_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
                                        lut_off, lookupNew, sizeof(lookupNew));
    LOG("[OLED] re-inject: 0x%08X\n", lut_inject);

    if (ksceOledSetBrightness && ksceOledGetBrightness)
        ksceOledSetBrightness(ksceOledGetBrightness());

    return lut_inject >= 0 ? 0 : (int)lut_inject;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void oled_enable_hooks(void) {
    if (g_hooks_active) return;

    tai_module_info_t info;
    info.size = sizeof(tai_module_info_t);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceOled", &info);
    LOG("[OLED] SceOled modinfo: 0x%08X  modid=0x%08X\n", ret, info.modid);
    if (ret < 0) { LOG("[OLED] SceOled not found\n"); return; }

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_BRIGHTNESS, (uintptr_t *)&ksceOledGetBrightness);
    LOG("[OLED] GetBrightness: 0x%08X -> %p\n", ret, ksceOledGetBrightness);

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, (uintptr_t *)&ksceOledSetBrightness);
    LOG("[OLED] SetBrightness: 0x%08X -> %p\n", ret, ksceOledSetBrightness);

    ret = module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_GET_DDB, (uintptr_t *)&ksceOledGetDDB);
    LOG("[OLED] GetDDB: 0x%08X -> %p\n", ret, ksceOledGetDDB);

    module_get_export_func(KERNEL_PID, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_COLOR_SPACE, (uintptr_t *)&ksceOledSetDisplayColorSpaceMode);

    if (ksceOledGetBrightness == NULL || ksceOledSetBrightness == NULL) {
        LOG("[OLED] Critical NID resolution failed\n"); return;
    }

    g_panel_type = oled_detect_panel();
    LOG("[OLED] Panel type: %d\n", g_panel_type);

    /* Load LUT into lookupNew */
    int lut_ok;
    if (g_config.oled_panel_lut_override && g_config.panel_lut_path[0] != '\0') {
        LOG("[OLED] Override LUT: %s\n", g_config.panel_lut_path);
        lut_ok = parse_lut_override(g_config.panel_lut_path, lookupNew);
    } else {
        lut_ok = parse_lut(g_panel_type, lookupNew);
    }

    if (lut_ok < 0) { LOG("[OLED] LUT load failed: %d\n", lut_ok); return; }

    /* Post-process */
    normalise_white_point(lookupNew);
    apply_color_bias(lookupNew);
    apply_night_mode(lookupNew);

    /*
     * C-4 fix: snapshot the post-processed LUT into lookupBase.
     * screen_filter_apply() copies lookupBase → lookupNew before
     * applying the filter, so repeated calls don't compound.
     */
    lut_memcpy(lookupBase, lookupNew, LUT_SIZE);

    /* Inject */
    uint32_t lut_off = panel_to_lut_offset(g_panel_type);
    LOG("[OLED] Inject at 0x%08X\n", lut_off);
    lut_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
                                        lut_off, lookupNew, sizeof(lookupNew));
    LOG("[OLED] taiInjectData: 0x%08X\n", lut_inject);

    if (lut_inject < 0) {
        LOG("[OLED] LUT inject failed — aborting hook installation\n");
        return;
    }

    ksceOledSetBrightness(ksceOledGetBrightness());

    oled_set_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &oled_set_brightness_ref, "SceOled", TAI_ANY_LIBRARY,
        NID_OLED_SET_BRIGHTNESS, hook_ksceOledSetBrightness);
    LOG("[OLED] SetBrightness hook: 0x%08X\n", oled_set_brightness_hook);

    power_set_max_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &power_set_max_brightness_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForOled);
    LOG("[OLED] PowerMaxBright hook: 0x%08X\n", power_set_max_brightness_hook);

    g_hooks_active = 1;
}

void oled_disable_hooks(void) {
    /* C-5 fix: NULL out function pointers so the hook body is safe */
    ksceOledGetBrightness = NULL;

    if (lut_inject >= 0) {
        taiInjectReleaseForKernel(lut_inject);
        lut_inject = -1;
    }
    if (oled_set_brightness_hook >= 0) {
        taiHookReleaseForKernel(oled_set_brightness_hook, oled_set_brightness_ref);
        oled_set_brightness_hook = -1;
    }
    if (power_set_max_brightness_hook >= 0) {
        taiHookReleaseForKernel(power_set_max_brightness_hook, power_set_max_brightness_ref);
        power_set_max_brightness_hook = -1;
        power_set_max_brightness_ref  = 0;
    }
    g_hooks_active = 0;
}

/* ------------------------------------------------------------------ */
/* Syscall exports                                                     */
/* ------------------------------------------------------------------ */

int vitabrightOledGetLevel(void) {
    int state;
    ENTER_SYSCALL(state);

    if (ksceOledGetBrightness == NULL) { EXIT_SYSCALL(state); return -1; }

    int brightness = ksceOledGetBrightness();
    int level;
    if      (brightness == 0)           level = -1;  /* screen off */
    else if (brightness == 1)           level = 16;  /* dim sentinel */
    else if (brightness <= 0xFFF)       level = 15;  /* lowest on-slider */
    else if (brightness >= 0x10000)     level = 0;   /* maximum */
    else {
        /* brightness = 0x1000 * (15 - level)  [for level 1..14]
         * → level = 15 - (brightness / 0x1000) */
        level = 15 - (brightness / 0x1000);
        if (level < 1)  level = 1;
        if (level > 14) level = 14;
    }

    EXIT_SYSCALL(state);
    return level;
}

int vitabrightOledSetLevel(unsigned int level) {
    int state;
    ENTER_SYSCALL(state);

    if (ksceOledSetBrightness == NULL) { EXIT_SYSCALL(state); return -1; }

    unsigned int brightness;
    if      (level > 16)   brightness = 0;          /* off */
    else if (level == 16)  brightness = 1;           /* dim sentinel */
    else if (level == 15)  brightness = 0xFFF;       /* just below 0x1000 */
    else if (level == 0)   brightness = 0x10000;     /* maximum */
    else                   brightness = (unsigned int)(0x1000 * (15 - (int)level)); /* levels 1-14 */

    isDimmingWorkAround = 0;
    ksceOledSetBrightness(brightness);
    isDimmingWorkAround = 1;

    EXIT_SYSCALL(state);
    return (int)level;
}

int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    ENTER_SYSCALL(state);
    ksceKernelMemcpyKernelToUser((void *)oledLut, lookupNew, LUT_SIZE);
    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]) {
    int state;
    ENTER_SYSCALL(state);

    if (!g_hooks_active) { EXIT_SYSCALL(state); return -1; }

    /* Update lookupNew and lookupBase in-place then re-inject.
     * Do NOT call oled_disable/enable_hooks here — that would re-read the
     * LUT from disk and destroy the user-supplied data. */
    ksceKernelMemcpyUserToKernel(lookupNew, (const void *)oledLut, LUT_SIZE);
    lut_memcpy(lookupBase, lookupNew, LUT_SIZE);
    oled_reinject_lut();

    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightOledReload(void) {
    int state;
    ENTER_SYSCALL(state);

    oled_disable_hooks();
    config_load();
    oled_enable_hooks();

    /* M-7 fix: re-apply screen filter after reload */
    screen_filter_load_config();
    screen_filter_apply(1 /* is_oled */);

    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightOledGetPanelType(void) {
    int state;
    ENTER_SYSCALL(state);
    int pt = g_panel_type;
    EXIT_SYSCALL(state);
    return pt;
}
