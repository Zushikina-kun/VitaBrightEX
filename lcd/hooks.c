#include "hooks.h"
#include "lcd_lut.h"
#include "../main.h"
#include "../log.h"
#include "../config.h"
#include "../taihen_extra.h"
#include <stdint.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/io/fcntl.h>
#include <taihen.h>

/* ------------------------------------------------------------------ */
/* NID constants                                                       */
/* ------------------------------------------------------------------ */
#define NID_LCD_GET_BRIGHTNESS     0x3A6D6AC3
#define NID_LCD_SET_BRIGHTNESS     0x581D3A87
#define NID_LCD_GET_COLOR_SPACE    0x17F66722
#define NID_LCD_SET_COLOR_SPACE    0xD40968FB
#define NID_POWER_SET_MAX_BRIGHT   0x77027B6B
#define NID_REGMGR_GET_KEY_INT     0x30977F95
#define NID_REGMGR_SET_KEY_INT     0x23B99BDE

#define REG_COLOR_SPACE   "/CONFIG/DISPLAY/color_space_mode"
#define REG_RGB_RANGE     "/CONFIG/DISPLAY/rgb_range_mode"

/* ------------------------------------------------------------------ */
/* Default brightness table (17 levels)                                */
/* ------------------------------------------------------------------ */
static const uint8_t lcd_brightness_default[LCD_LUT_LEVELS] = {
    1, 3, 5, 8, 13, 20, 29, 41, 57, 76, 95, 116, 137, 161, 190, 220, 255
};

static uint8_t lcd_brightness_values[LCD_LUT_LEVELS];

/* ------------------------------------------------------------------ */
/* Hook handles                                                        */
/* ------------------------------------------------------------------ */
static SceUID lcd_table_inject          = -1;
static SceUID lcd_set_brightness_hook   = -1;
static SceUID power_set_max_bright_hook = -1;

static tai_hook_ref_t lcd_set_brightness_ref   = -1;
static tai_hook_ref_t power_set_max_bright_ref = 0;

static int (*ksceLcdGetBrightness)(void)                     = NULL;
static int (*ksceLcdSetBrightness)(unsigned int brightness)  = NULL;
static int (*ksceLcdSetDisplayColorSpaceMode)(int mode)      = NULL;
static int (*ksceRegMgrGetKeyInt)(const char *key, int *val) = NULL;
static int (*ksceRegMgrSetKeyInt)(const char *key, int val)  = NULL;

static int g_lcd_hooks_active = 0;

/* ------------------------------------------------------------------ */
/* Brightness index helpers                                            */
/*                                                                     */
/* H-4 fix: SceLcd's GetBrightness returns a raw 16-bit PWM value     */
/* (same 0-65536 range as OLED).  The brightness table maps 17 levels  */
/* to byte values 0-255 which the driver converts to PWM.             */
/* We reverse-lookup the nearest table entry by comparing PWM values. */
/* ------------------------------------------------------------------ */

/*
 * Convert a raw PWM brightness value to a table index [0, 16].
 * The table maps index → 8-bit level, and the driver scales that to
 * a 16-bit PWM linearly: pwm = (level * 65535) / 255.
 * We find the nearest index by minimising |pwm - expected_pwm(i)|.
 */
static int lcd_brightness_to_index(unsigned int brightness) {
    if (brightness == 0) return 16;  /* off / minimum */
    int best_idx = 0;
    unsigned int best_dist = 0xFFFFFFFFu;
    for (int i = 0; i < LCD_LUT_LEVELS; i++) {
        unsigned int expected = ((unsigned int)lcd_brightness_values[i] * 65535u) / 255u;
        unsigned int dist = (brightness > expected)
                            ? (brightness - expected)
                            : (expected - brightness);
        if (dist < best_dist) { best_dist = dist; best_idx = i; }
    }
    return best_idx;
}

/* ------------------------------------------------------------------ */
/* LCD brightness LUT file loader                                      */
/* Format: one decimal value per line (0-255), 17 lines, # = comment. */
/* ------------------------------------------------------------------ */

static int lcd_parse_lut_file(const char *path, uint8_t out[LCD_LUT_LEVELS]) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 6);
    if (fd < 0) return fd;

    int count = 0;
    char line[16];
    int li = 0;

    while (count < LCD_LUT_LEVELS) {
        char c;
        int r = ksceIoRead(fd, &c, 1);
        if (r <= 0) break;
        if (c == '\r') continue;
        if (c == '\n' || li == (int)sizeof(line) - 1) {
            line[li] = '\0';
            li = 0;
            if (line[0] == '#' || line[0] == '\0') continue;
            int val = 0;
            int i = 0;
            for (; line[i] >= '0' && line[i] <= '9'; i++)
                val = val * 10 + (line[i] - '0');
            if (val < 0)   val = 0;
            if (val > 255) val = 255;
            out[count++] = (uint8_t)val;
        } else {
            line[li++] = c;
        }
    }

    ksceIoClose(fd);
    if (count < LCD_LUT_LEVELS) {
        LOG("[LCD] LUT file incomplete: %d/%d\n", count, LCD_LUT_LEVELS);
        return -1;
    }
    LOG("[LCD] LUT loaded from %s\n", path);
    return 0;
}

static void lcd_load_lut(void) {
    for (int i = 0; i < LCD_LUT_LEVELS; i++)
        lcd_brightness_values[i] = lcd_brightness_default[i];

    int r = lcd_parse_lut_file(LCD_LUT_FILE1, lcd_brightness_values);
    if (r < 0) lcd_parse_lut_file(LCD_LUT_FILE2, lcd_brightness_values);
}

/* ------------------------------------------------------------------ */
/* IPS / colour enhancement                                            */
/* ------------------------------------------------------------------ */

static void lcd_apply_color_enhancement(void) {
    int do_csm   = g_config.lcd_color_space_mode;
    int do_rgb   = g_config.lcd_rgb_range_mode;
    /* lcd_saturation_boost is an alias for lcd_ips_enhance — either enables
     * the live colour-space driver call */
    int do_live  = g_config.lcd_ips_enhance || g_config.lcd_saturation_boost;

    if (!do_csm && !do_rgb && !do_live) return;

    if (ksceRegMgrSetKeyInt != NULL) {
        if (do_csm) {
            ksceRegMgrSetKeyInt(REG_COLOR_SPACE, 1);
            LOG("[LCD] color_space_mode = 1\n");
        }
        if (do_rgb) {
            ksceRegMgrSetKeyInt(REG_RGB_RANGE, 1);
            LOG("[LCD] rgb_range_mode = 1\n");
        }
    } else {
        LOG("[LCD] SceRegMgr unavailable\n");
    }

    if (do_live && ksceLcdSetDisplayColorSpaceMode != NULL) {
        int mode = do_csm ? 1 : 0;
        int ret = ksceLcdSetDisplayColorSpaceMode(mode);
        LOG("[LCD] SetDisplayColorSpaceMode(%d): 0x%08X\n", mode, ret);
    }
}

/* ------------------------------------------------------------------ */
/* Hooks                                                               */
/* ------------------------------------------------------------------ */

int hook_ksceLcdSetBrightness(unsigned int brightness) {
    if (brightness != 1)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    /* Inactivity dim — only allow if the current level is above idx 4
     * (the middle of the default range), to prevent paradoxical brighten */
    if (ksceLcdGetBrightness == NULL)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    unsigned int old_brightness = (unsigned int)ksceLcdGetBrightness();
    int old_index = lcd_brightness_to_index(old_brightness);

    /* Allow dim only if current brightness maps to index > 4 */
    if (old_index > 4)
        return TAI_CONTINUE(int, lcd_set_brightness_ref, brightness);

    /* Already very dim — keep current to avoid raising brightness */
    return TAI_CONTINUE(int, lcd_set_brightness_ref, old_brightness);
}

int hook_kscePowerSetDisplayMaxBrightnessForLcd(int limit) {
    (void)limit;
    if (power_set_max_bright_ref == 0) return 0;
    return TAI_CONTINUE(int, power_set_max_bright_ref, 0x10000);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void lcd_enable_hooks(void) {
    if (g_lcd_hooks_active) return;

    lcd_load_lut();

    tai_module_info_t info;
    info.size = sizeof(tai_module_info_t);
    int ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLcd", &info);
    LOG("[LCD] SceLcd modinfo: 0x%08X  modid=0x%08X\n", ret, info.modid);
    if (ret < 0) { LOG("[LCD] SceLcd not found\n"); return; }

    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_GET_BRIGHTNESS, (uintptr_t *)&ksceLcdGetBrightness);
    LOG("[LCD] GetBrightness: 0x%08X -> %p\n", ret, ksceLcdGetBrightness);

    ret = module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, (uintptr_t *)&ksceLcdSetBrightness);
    LOG("[LCD] SetBrightness: 0x%08X -> %p\n", ret, ksceLcdSetBrightness);

    module_get_export_func(KERNEL_PID, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_COLOR_SPACE, (uintptr_t *)&ksceLcdSetDisplayColorSpaceMode);

    if (ksceLcdGetBrightness == NULL || ksceLcdSetBrightness == NULL) {
        LOG("[LCD] Critical NID resolution failed\n"); return;
    }

    /* SceRegMgr for registry colour tweaks */
    tai_module_info_t reginfo;
    reginfo.size = sizeof(tai_module_info_t);
    if (taiGetModuleInfoForKernel(KERNEL_PID, "SceRegMgr", &reginfo) >= 0) {
        module_get_export_func(KERNEL_PID, "SceRegMgr", TAI_ANY_LIBRARY,
            NID_REGMGR_SET_KEY_INT, (uintptr_t *)&ksceRegMgrSetKeyInt);
        module_get_export_func(KERNEL_PID, "SceRegMgr", TAI_ANY_LIBRARY,
            NID_REGMGR_GET_KEY_INT, (uintptr_t *)&ksceRegMgrGetKeyInt);
        LOG("[LCD] SceRegMgr set=%p get=%p\n", ksceRegMgrSetKeyInt, ksceRegMgrGetKeyInt);
    } else {
        LOG("[LCD] SceRegMgr not found\n");
    }

    lcd_apply_color_enhancement();

    /*
     * C-3 fix: removed the unsafe scan-from-function-address approach.
     * Use versioned offsets only. The fallback (default case) covers
     * 3.65–3.74 which all share the same SceLcd binary layout.
     */
    uint32_t table_off;
    switch (sw_version >> 16) {
    case 0x360:
        table_off = 0x1B00;
        break;
    case 0x365: case 0x367: case 0x368: case 0x369: case 0x370:
    case 0x371: case 0x372: case 0x373: case 0x374:
    default:
        /* 3.65–3.74 all share the same SceLcd layout */
        table_off = 0x1B48;
        break;
    }
    LOG("[LCD] Table offset 0x%08X for fw 0x%08X\n", table_off, sw_version);

    lcd_table_inject = taiInjectDataForKernel(KERNEL_PID, info.modid, 0,
                                              table_off,
                                              lcd_brightness_values,
                                              sizeof(lcd_brightness_values));
    LOG("[LCD] taiInjectData: 0x%08X\n", lcd_table_inject);

    ksceLcdSetBrightness(ksceLcdGetBrightness());

    lcd_set_brightness_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &lcd_set_brightness_ref, "SceLcd", TAI_ANY_LIBRARY,
        NID_LCD_SET_BRIGHTNESS, hook_ksceLcdSetBrightness);
    LOG("[LCD] SetBrightness hook: 0x%08X\n", lcd_set_brightness_hook);

    power_set_max_bright_hook = taiHookFunctionExportForKernel(KERNEL_PID,
        &power_set_max_bright_ref, "ScePower", TAI_ANY_LIBRARY,
        NID_POWER_SET_MAX_BRIGHT, hook_kscePowerSetDisplayMaxBrightnessForLcd);
    LOG("[LCD] PowerMaxBright hook: 0x%08X\n", power_set_max_bright_hook);

    g_lcd_hooks_active = 1;
}

void lcd_disable_hooks(void) {
    ksceLcdGetBrightness = NULL;  /* NULL out before releasing hook */

    if (lcd_table_inject >= 0) {
        taiInjectReleaseForKernel(lcd_table_inject);
        lcd_table_inject = -1;
    }
    if (lcd_set_brightness_hook >= 0) {
        taiHookReleaseForKernel(lcd_set_brightness_hook, lcd_set_brightness_ref);
        lcd_set_brightness_hook = -1;
    }
    if (power_set_max_bright_hook >= 0) {
        taiHookReleaseForKernel(power_set_max_bright_hook, power_set_max_bright_ref);
        power_set_max_bright_hook = -1;
        power_set_max_bright_ref  = 0;
    }
    g_lcd_hooks_active = 0;
}

/* ------------------------------------------------------------------ */
/* Syscall exports                                                     */
/* ------------------------------------------------------------------ */

int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]) {
    int state;
    ENTER_SYSCALL(state);
    ksceKernelMemcpyKernelToUser((void *)out, lcd_brightness_values,
                                 LCD_LUT_LEVELS * sizeof(uint8_t));
    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]) {
    int state;
    ENTER_SYSCALL(state);

    lcd_disable_hooks();
    ksceKernelMemcpyUserToKernel(lcd_brightness_values, (const void *)in,
                                 LCD_LUT_LEVELS * sizeof(uint8_t));
    lcd_enable_hooks();

    EXIT_SYSCALL(state);
    return 0;
}

int vitabrightLcdReapplyColor(void) {
    int state;
    ENTER_SYSCALL(state);
    lcd_apply_color_enhancement();
    EXIT_SYSCALL(state);
    return 0;
}
