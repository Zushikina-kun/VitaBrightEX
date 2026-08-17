#pragma once

extern unsigned int sw_version;

/*
 * g_is_oled: 1 = PS Vita 1000 (OLED), 0 = PS Vita 2000 (LCD).
 * Set at module_start and used by screen_filter to avoid trusting
 * userland-supplied is_oled arguments (L-5 fix).
 */
extern int g_is_oled;
