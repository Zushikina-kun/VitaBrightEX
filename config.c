#include "config.h"
#include "log.h"
#include <psp2kern/io/fcntl.h>

/* Global config instance with sensible defaults */
VitaBrightConfig g_config = {
    .oled_panel_lut_override  = 0,
    .panel_lut_path           = "",
    .color_r_bias             = 0,
    .color_g_bias             = 0,
    .color_b_bias             = 0,
    .night_mode_enabled       = 0,
    .night_mode_threshold     = 6,
    .lcd_color_space_mode     = 1,
    .lcd_rgb_range_mode       = 1,
    .lcd_saturation_boost     = 1,
    .lcd_ips_enhance          = 1,
    .oled_dim_workaround      = 1,
    .filter_cct               = 6500,
    .filter_gamma             = 1.0f,
    .filter_contrast          = 1.0f,
    .filter_brightness        = 0.0f,
    .filter_invert            = 0,
    .filter_panel_enhance     = 0,
};

/* ------------------------------------------------------------------ */
/* Minimal string helpers (no libc in kernel)                          */
/* ------------------------------------------------------------------ */

/* Exact full-string comparison (equivalent to strcmp) */
static int cfg_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Copy at most dst_size-1 chars, always null-terminate */
static void cfg_strncpy(char *dst, const char *src, int dst_size) {
    int i;
    for (i = 0; i < dst_size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* Simple atoi for signed integers — stops at first non-digit (incl. '.') */
static int cfg_atoi(const char *s) {
    int neg = 0, val = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

/*
 * Parse a decimal float string (e.g. "-0.75", "1.2", "2") into a float.
 * Handles negative fractions correctly (e.g. "-0.5" → -0.5f).
 */
static float cfg_atof(const char *s) {
    /* Check sign */
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    /* Integer part */
    float val = 0.0f;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0f + (float)(*s - '0');
        s++;
    }

    /* Fractional part */
    if (*s == '.') {
        s++;
        float place = 0.1f;
        while (*s >= '0' && *s <= '9') {
            val += (float)(*s - '0') * place;
            place *= 0.1f;
            s++;
        }
    }

    return neg ? -val : val;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

/*
 * Reads one line from fd into buf (up to len-1 bytes).
 * Returns: number of characters stored (>= 0).
 *          -1 on I/O error.
 * EOF is detected by the caller: if the return is 0 AND the previous
 * read of the underlying fd returned <= 0, we're at EOF.
 * We expose this by returning -1 when the very first byte read fails.
 */
static int cfg_readline(SceUID fd, char *buf, int len) {
    int total = 0;
    char c;

    while (total < len - 1) {
        int r = ksceIoRead(fd, &c, 1);
        if (r < 0) {
            buf[total] = '\0';
            return -1;   /* I/O error */
        }
        if (r == 0) {
            /* EOF reached */
            buf[total] = '\0';
            return (total == 0) ? -1 : total;  /* -1 signals "nothing more" */
        }
        if (c == '\r') continue;   /* strip CR from Windows line endings */
        if (c == '\n') break;      /* end of line */
        buf[total++] = c;
    }

    buf[total] = '\0';
    return total;
}

/* Trim leading whitespace, return pointer to first non-space */
static const char *cfg_ltrim(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Find '=' in key=value line; return pointer after '=' or NULL */
static const char *cfg_split(const char *line) {
    while (*line && *line != '=') line++;
    if (*line != '=') return 0;
    return line + 1;
}

static void cfg_apply(const char *key, const char *val) {
    /* Use cfg_streq for exact match to prevent prefix false-positives (H-5) */
    if      (cfg_streq(key, "oled_panel_lut_override"))
        g_config.oled_panel_lut_override = cfg_atoi(val);
    else if (cfg_streq(key, "panel_lut_path"))
        cfg_strncpy(g_config.panel_lut_path, val, sizeof(g_config.panel_lut_path));
    else if (cfg_streq(key, "color_r_bias"))
        g_config.color_r_bias = cfg_atoi(val);
    else if (cfg_streq(key, "color_g_bias"))
        g_config.color_g_bias = cfg_atoi(val);
    else if (cfg_streq(key, "color_b_bias"))
        g_config.color_b_bias = cfg_atoi(val);
    else if (cfg_streq(key, "night_mode_enabled"))
        g_config.night_mode_enabled = cfg_atoi(val);
    else if (cfg_streq(key, "night_mode_threshold"))
        g_config.night_mode_threshold = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_color_space_mode"))
        g_config.lcd_color_space_mode = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_rgb_range_mode"))
        g_config.lcd_rgb_range_mode = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_saturation_boost"))
        g_config.lcd_saturation_boost = cfg_atoi(val);
    else if (cfg_streq(key, "lcd_ips_enhance"))
        g_config.lcd_ips_enhance = cfg_atoi(val);
    else if (cfg_streq(key, "oled_dim_workaround"))
        g_config.oled_dim_workaround = cfg_atoi(val);
    else if (cfg_streq(key, "filter_cct"))
        g_config.filter_cct = cfg_atoi(val);
    else if (cfg_streq(key, "filter_gamma"))
        g_config.filter_gamma = cfg_atof(val);  /* fixed: uses cfg_atof for correct sign (M-5) */
    else if (cfg_streq(key, "filter_contrast"))
        g_config.filter_contrast = cfg_atof(val);
    else if (cfg_streq(key, "filter_brightness"))
        g_config.filter_brightness = cfg_atof(val);
    else if (cfg_streq(key, "filter_invert"))
        g_config.filter_invert = cfg_atoi(val);
    else if (cfg_streq(key, "filter_panel_enhance"))
        g_config.filter_panel_enhance = cfg_atoi(val);
    /* Unknown keys silently ignored for forward compatibility */
}

int config_load(void) {
    SceUID fd = ksceIoOpen(CFG_FILE1, SCE_O_RDONLY, 6);
    if (fd < 0) {
        fd = ksceIoOpen(CFG_FILE2, SCE_O_RDONLY, 6);
        if (fd < 0) {
            LOG("[CFG] No config file found, using defaults\n");
            return 0;
        }
        LOG("[CFG] Loaded from %s\n", CFG_FILE2);
    } else {
        LOG("[CFG] Loaded from %s\n", CFG_FILE1);
    }

    char line[CFG_MAX_LINE];

    /* Fixed EOF loop (C-1): cfg_readline returns -1 on EOF or I/O error */
    while (1) {
        int n = cfg_readline(fd, line, sizeof(line));
        if (n < 0) break;   /* EOF or error — stop cleanly */

        const char *trimmed = cfg_ltrim(line);
        if (!trimmed[0] || trimmed[0] == '#' || trimmed[0] == ';') continue;

        const char *val = cfg_split(trimmed);
        if (!val) continue;

        /* Extract key: everything before '=', trailing-space stripped */
        int key_len = (int)(val - trimmed) - 1;  /* -1 to exclude '=' */
        if (key_len <= 0 || key_len >= CFG_MAX_LINE) continue;

        char key[CFG_MAX_LINE];
        int k = 0;
        for (int i = 0; i < key_len; i++) {
            char ch = trimmed[i];
            if (ch != ' ' && ch != '\t') key[k++] = ch;
        }
        key[k] = '\0';

        val = cfg_ltrim(val);

        LOG("[CFG] key=\"%s\" val=\"%s\"\n", key, val);
        cfg_apply(key, val);
    }

    ksceIoClose(fd);
    return 0;
}
