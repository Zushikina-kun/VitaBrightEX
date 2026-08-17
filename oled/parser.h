#pragma once
#include "lut.h"

/*
 * Parse a LUT file at an explicit path.
 * Returns 0 on success, negative on failure.
 */
int parse_lut_from_file(const char *path, unsigned char lookupNew[LUT_SIZE]);

/*
 * Auto-select and parse the best available LUT for the given panel type.
 * Falls back to generic user file, then returns error if nothing found.
 * panel_type: OLED_PANEL_4 / OLED_PANEL_5 / OLED_PANEL_6 / OLED_PANEL_UNKNOWN
 */
int parse_lut(int panel_type, unsigned char lookupNew[LUT_SIZE]);

/*
 * Parse LUT from an explicit path (config override).
 */
int parse_lut_override(const char *path, unsigned char lookupNew[LUT_SIZE]);
