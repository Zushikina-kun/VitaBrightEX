#include "lut.h"
#include "parser.h"
#include "../log.h"
#include <psp2kern/io/fcntl.h>

/* ------------------------------------------------------------------ */
/* Low-level hex parsing                                               */
/* ------------------------------------------------------------------ */

static int is_hex(unsigned char c) {
    return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') ||
           ('a' <= c && c <= 'f');
}

static int parse_hex_digit(unsigned char c) {
    if (c >= 'a') return c - 'a' + 10;
    if (c >= 'A') return c - 'A' + 10;
    return c - '0';
}

static int hex_to_int(unsigned char c[2]) {
    if (c[0] == '#') return -2;   /* comment sentinel */
    if (!is_hex(c[0]) || !is_hex(c[1])) return -1;
    return 16 * parse_hex_digit(c[0]) + parse_hex_digit(c[1]);
}

static int parse_hex(SceUID fd) {
    unsigned char hex_buf[2] = {0};
    /* Skip Windows CR */
    unsigned char ch;
    int r;
    do {
        r = ksceIoRead(fd, &ch, 1);
        if (r != 1) return -3;
    } while (ch == '\r');
    hex_buf[0] = ch;

    do {
        r = ksceIoRead(fd, &ch, 1);
        if (r != 1) return -3;
    } while (ch == '\r');
    hex_buf[1] = ch;

    return hex_to_int(hex_buf);
}

/* Skip to end-of-line (for comments) */
static void skip_line(SceUID fd) {
    unsigned char c = 0;
    while (c != '\n') {
        if (ksceIoRead(fd, &c, 1) != 1) return;
    }
}

/* Parse one 21-byte LUT row.  Returns 0 on success, -2 on comment, <0 on error. */
static int parse_line(SceUID fd, unsigned char lut_line[LUT_LINE_SIZE]) {
    for (int i = 0; i < LUT_LINE_SIZE; i++) {
        int val = parse_hex(fd);

        if (val == -2) {
            /* Comment line — skip rest and signal caller */
            skip_line(fd);
            return -2;
        }
        if (val < 0) return val;

        lut_line[i] = (unsigned char)val;

        unsigned char sep = 0;
        /* Read CR-stripped separator */
        int r;
        do {
            r = ksceIoRead(fd, &sep, 1);
            if (r != 1) return (i == LUT_LINE_SIZE - 1) ? 0 : -1;
        } while (sep == '\r');

        if (i != LUT_LINE_SIZE - 1) {
            /* Expect space between values */
            if (sep != ' ') return -1;
        } else {
            /* Last value: expect newline (or EOF) */
            if (sep != '\n') return -1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * Try to open and fully parse a LUT file.
 * Returns 0 on success, negative on failure.
 */
int parse_lut_from_file(const char *path, unsigned char lookupNew[LUT_SIZE]) {
    SceUID fd = ksceIoOpen(path, SCE_O_RDONLY, 6);
    if (fd < 0) return fd;

    LOG("[LUT] Parsing: %s\n", path);

    int rows_parsed = 0;
    while (rows_parsed < LUT_ROWS) {
        int ret = parse_line(fd, &lookupNew[rows_parsed * LUT_LINE_SIZE]);
        if (ret == -2) continue; /* comment, try next */
        if (ret < 0) {
            ksceIoClose(fd);
            LOG("[LUT] Parse error at row %d: %d\n", rows_parsed, ret);
            return ret;
        }
        rows_parsed++;
    }

    ksceIoClose(fd);
    LOG("[LUT] OK (%d rows)\n", rows_parsed);
    return 0;
}

/*
 * Try two paths (primary / fallback) and return 0 when either succeeds.
 */
static int try_parse(const char *p1, const char *p2, unsigned char out[LUT_SIZE]) {
    int r = parse_lut_from_file(p1, out);
    if (r >= 0) return r;
    return parse_lut_from_file(p2, out);
}

/*
 * Auto-select a LUT based on panel type, with full fallback chain:
 *   panel-specific -> generic user file -> built-in defaults
 *
 * panel_type: OLED_PANEL_4, OLED_PANEL_5, OLED_PANEL_6, OLED_PANEL_UNKNOWN
 */
int parse_lut(int panel_type, unsigned char lookupNew[LUT_SIZE]) {
    int r = -1;

    switch (panel_type) {
    case OLED_PANEL_4:
        r = try_parse(LUT_FILE_P4_1, LUT_FILE_P4_2, lookupNew);
        break;
    case OLED_PANEL_5:
        r = try_parse(LUT_FILE_P5_1, LUT_FILE_P5_2, lookupNew);
        break;
    case OLED_PANEL_6:
        r = try_parse(LUT_FILE_P6_1, LUT_FILE_P6_2, lookupNew);
        break;
    default:
        break;
    }

    /* Fall through to generic user file if panel-specific not found */
    if (r < 0) {
        r = try_parse(LUT_FILE1, LUT_FILE2, lookupNew);
    }

    if (r < 0) {
        LOG("[LUT] No LUT file found for panel %d\n", panel_type);
    }
    return r;
}

/*
 * Load LUT from an explicit path (for config override).
 */
int parse_lut_override(const char *path, unsigned char lookupNew[LUT_SIZE]) {
    return parse_lut_from_file(path, lookupNew);
}
