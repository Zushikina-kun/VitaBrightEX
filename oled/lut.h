#pragma once

/*
 * LUT file paths — generic fallback (used when no panel-specific file found)
 */
#define LUT_FILE1 "ur0:tai/vitabright_lut.txt"
#define LUT_FILE2 "ux0:tai/vitabright_lut.txt"

/*
 * Panel-specific LUT files (supplier_elective_data low byte)
 * Panel 4  = AMS495QA04  (supplier_elective_data & 0xFF == 4 or data == 0x806)
 * Panel 5  = AMS495QA01  (supplier_elective_data & 0xFF == 5 or data == 0x805)
 * Default  = older / unknown variants
 */
#define LUT_FILE_P4_1  "ur0:tai/vitabright_lut_p4.txt"
#define LUT_FILE_P4_2  "ux0:tai/vitabright_lut_p4.txt"
#define LUT_FILE_P5_1  "ur0:tai/vitabright_lut_p5.txt"
#define LUT_FILE_P5_2  "ux0:tai/vitabright_lut_p5.txt"
#define LUT_FILE_P6_1  "ur0:tai/vitabright_lut_p6.txt"   /* future / replacement panels */
#define LUT_FILE_P6_2  "ux0:tai/vitabright_lut_p6.txt"

/* LUT geometry: 17 rows × 21 bytes = 357 bytes total */
#define LUT_SIZE       (357)
#define LUT_LINE_SIZE  (21)
#define LUT_ROWS       (LUT_SIZE / LUT_LINE_SIZE)   /* 17 */

/* Panel type IDs returned by oled_detect_panel() */
#define OLED_PANEL_UNKNOWN  0
#define OLED_PANEL_4        4   /* AMS495QA04 */
#define OLED_PANEL_5        5   /* AMS495QA01 */
#define OLED_PANEL_6        6   /* other / replacement */