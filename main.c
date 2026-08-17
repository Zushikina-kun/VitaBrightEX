#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysroot.h>
#include <taihen.h>

#include "main.h"
#include "config.h"
#include "lcd/hooks.h"
#include "log.h"
#include "oled/hooks.h"
#include "screen_filter.h"

unsigned int sw_version = 0;
int g_is_oled = 0;

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    (void)argc; (void)args;
    LOG("vitabrightex started...\n");

    sw_version = ksceKernelSysrootGetSystemSwVersion();
    LOG("[OS] version: %08X\n", sw_version);

    /* Load user configuration first — hooks and filter read g_config */
    config_load();

    /* Boot type indicator 1.
     * See https://wiki.henkaku.xyz/vita/Sysroot#Boot_type_indicator_1
     * Bit 0 set + bit 3 set (mask 0x09) = LCD device                 */
    int is_lcd = *(uint8_t *)(ksceKernelSysrootGetKblParam() + 0xE8) & 9;
    LOG("[OS] isLcd: %d\n", is_lcd);
    g_is_oled = !is_lcd;

    if (is_lcd) {
        lcd_enable_hooks();
    } else {
        oled_enable_hooks();
    }

    /* Apply screen filter (Rosalina-equivalent colour pipeline) */
    screen_filter_load_config();
    screen_filter_apply(g_is_oled);

    return SCE_KERNEL_START_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Syscall: reload everything from disk (config + LUT + filter)       */
/* ------------------------------------------------------------------ */
int vitabrightReload(void) {
    int state;
    ENTER_SYSCALL(state);

    oled_disable_hooks();
    lcd_disable_hooks();

    config_load();

    oled_enable_hooks();
    lcd_enable_hooks();

    screen_filter_load_config();
    screen_filter_apply(g_is_oled);

    EXIT_SYSCALL(state);
    return 0;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc; (void)args;
    /* Restore identity filter before unloading */
    screen_filter_reset(g_is_oled);
    oled_disable_hooks();
    lcd_disable_hooks();
    return SCE_KERNEL_STOP_SUCCESS;
}
