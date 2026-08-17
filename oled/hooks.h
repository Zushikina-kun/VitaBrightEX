#pragma once
#include <stdint.h>
#include "lut.h"

/* Function pointers resolved by NID */
extern int (*ksceOledGetBrightness)(void);
extern int (*ksceOledSetBrightness)(unsigned int brightness);
extern int (*ksceOledGetDDB)(uint16_t *supplier_id, uint16_t *supplier_elective_data);

/*
 * lookupBase — post-load, post-normalise snapshot (screen_filter input).
 * lookupNew  — currently injected state.
 * screen_filter copies lookupBase → lookupNew before modifying, so
 * repeated filter applications never compound (C-4 fix).
 */
extern unsigned char lookupBase[LUT_SIZE];
extern unsigned char lookupNew[LUT_SIZE];

/* Lifecycle */
void oled_enable_hooks(void);
void oled_disable_hooks(void);

/* Panel detection */
int oled_detect_panel(void);

/*
 * Re-inject lookupNew without a full reload (called by screen_filter
 * after it has modified lookupNew in-place).
 */
int oled_reinject_lut(void);

/* Syscall exports */
int vitabrightOledGetLevel(void);
int vitabrightOledSetLevel(unsigned int level);
int vitabrightOledGetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledSetLut(unsigned char oledLut[LUT_SIZE]);
int vitabrightOledReload(void);
int vitabrightOledGetPanelType(void);
