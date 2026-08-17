#pragma once
#include <stdint.h>
#include "lcd_lut.h"

void lcd_enable_hooks(void);
void lcd_disable_hooks(void);

/* Syscall exports */
int vitabrightLcdGetBrightnessValues(uint8_t out[LCD_LUT_LEVELS]);
int vitabrightLcdSetBrightnessValues(uint8_t in[LCD_LUT_LEVELS]);
int vitabrightLcdReapplyColor(void);
