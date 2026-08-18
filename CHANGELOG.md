# Changelog

All notable changes from the original [vitabright](https://github.com/devnoname120/vitabright)
and between VitaBrightEX versions are documented here.

---

## VitaBrightEX v1.2

### Fixed

**`vitabrightOledSetLut` destroyed user LUT (critical editor bug)**
The syscall called `oled_disable_hooks()` + `oled_enable_hooks()` which
re-read the LUT from disk, overwriting the user-supplied data. Every edit
in the LUT editor was silently discarded. Fixed: now updates `lookupNew`
and `lookupBase` in-place and calls `oled_reinject_lut()` directly.

**`oled_enable_hooks` installed hooks even when LUT inject failed**
If `taiInjectDataForKernel` returned an error, the brightness and power
hooks were still installed and `g_hooks_active` was set to 1. Fixed: early
return if inject fails.

**LCD `filter_brightness` scaling was 2× too strong**
`bright_offset` was scaled by `0x3FF` (1023) then clamped to `±0x1FF` (511),
wasting the upper half of the brightness range and doubling the effect in
the lower half. Fixed: scaled by `0x1FF` so `brightness=±1.0` maps exactly
to the `±0x1FF` clamp.

**`lcd_saturation_boost` config key had no effect**
The key was parsed and stored but never read in `lcd_apply_color_enhancement`.
Fixed: now treated as an alias for `lcd_ips_enhance` — either one enables
the live colour-space driver call.

**`log.c` used `extern` declaration instead of proper header**
`ksceIoMkdir` was declared with a bare `extern` without a proper include.
Fixed: uses `<psp2kern/io/stat.h>`.

**LUT editor: texture NULL dereference if resource files missing from VPK**
`vita2d_load_PNG_file` / `vita2d_load_JPEG_file` results were passed directly
to `vita2d_draw_texture` with no NULL check. Fixed: allocates a 1×1 black
placeholder texture if a resource file fails to load.

---

## VitaBrightEX v1.1

### Fixed

**`vitabrightOledGetLevel` off-by-one formula**
Was: `16 - ((brightness + 0x1000) / 0x1000)`
Correct: `15 - (brightness / 0x1000)`
All 17 levels (0–16) now round-trip perfectly between SetLevel and GetLevel.

**LUT editor: `screenLevel` poll timer never updated**
`lastPollTime` was set once at startup and never reset after each poll,
causing continuous `vitabrightOledGetLevel` syscall calls every frame.
Fixed: `lastPollTime` is updated after every 500ms poll.

**LUT editor: uninitialised `pvf` pointer freed on exit**
`vita2d_free_pvf(pvf)` was called but `pvf` was never assigned.
Fixed: removed the dead `vita2d_free_pvf` call.

**LUT editor: `writeLut` missing trailing newline (crash issue #7)**
The last row of the written LUT file had no terminating `\n`, causing
the vitabright parser to treat it as truncated and crash on reload.
Fixed: `fprintf(f, "%02X\n", ...)` on every row including the last.

**LUT editor: crash on Vita 2000 (issue #6 — C2-12828-1)**
App called OLED-only syscalls on an LCD unit. Fixed: detects LCD at startup
via `vitabrightOledGetLevel` return value; shows LCD info screen instead.

**LUT editor: generic crash error screen replaced with detailed message**
When `vitabrightOledGetLut` fails, the app now shows the actual return code
and a clear "check plugin version" message.

### Added

**LUT editor: panel type display**
Shows detected OLED panel name (AMS495QA01 / AMS495QA04 / replacement / unknown).

**LUT editor: screen filter editor**
Press `L2` to cycle through: LUT edit → CCT → Gamma → Contrast → Brightness.
Hold `R2` for 10× faster adjustment. Changes apply live.

**LUT editor: `START` saves LUT + filter to disk**
Saves LUT to `vitabright_lut.txt` and appends/overwrites filter parameters
in `vitabrightex.cfg`.

**LUT editor: `L1`/`R1` jump cursor by 7 bytes** (one colour group at a time).

**LUT editor: LCD colour enhancement status display**
On Vita 2000, shows active colour enhancement mode and allows filter editing.

---

## VitaBrightEX v1.0 (initial release)

### New vs. original vitabright

**Firmware compatibility (3.60–3.74+)**
Replaced all hardcoded byte offsets with `module_get_export_func()` NID-based
resolution from taihenModuleUtils.

**OLED: per-panel LUT auto-selection**
Reads `supplier_elective_data` from the DDB. Auto-loads `vitabright_lut_p4.txt`
(AMS495QA04), `vitabright_lut_p5.txt` (AMS495QA01), `vitabright_lut_p6.txt`
(replacement panels), or `vitabright_lut.txt` (fallback).

**OLED: white-point normalisation**
Normalises R/B channel ratios against the G anchor across all dim rows.
Fixes the red-screen effect on affected panels without requiring a custom LUT.

**OLED: colour bias (`color_r/g/b_bias`)**
Per-channel additive bias for fine-tuning remaining tints.

**OLED: night/warm mode**
Amber tint applied at and below a configurable brightness threshold.

**OLED: `lookupBase` / `lookupNew` split**
`lookupBase` holds the clean post-load LUT; `lookupNew` is the currently
injected state. Prevents screen filter from compounding on every re-apply.

**LCD (Vita 2000): colour enhancement**
Registry writes for `color_space_mode` and `rgb_range_mode`; live driver
`SetColorSpaceMode` call with no reboot required.

**LCD: user-editable brightness curve** via `vitabright_lcd_lut.txt`.

**LCD: fixed dim workaround**
Replaced incorrect linear brightness-to-index mapping with nearest-neighbour
reverse lookup against the actual brightness table.

**Screen filter (both models)**
CCT colour temperature (Planckian locus), gamma, contrast, brightness, hardware
invert via `sceDisplaySetInvertColorsForDriver`. IFTU 3×3 CSC matrix for LCD.
IPS panel linearisation curve (`filter_panel_enhance`).

**Config system**
`vitabrightex.cfg` read from `ur0:/tai/` or `ux0:/tai/`. All options optional.

### Fixed (bugs in original vitabright)

- Config EOF loop could consume extra bytes (C-1)
- `fp_to_u8` spurious `>>8` produced near-zero filter output (M-3)
- G channel not included in CCT white-point correction (H-2)
- `fp_pow` could enter near-infinite loop for dark LUT rows with high gamma (L-4)
- Negative float config values (e.g. `-0.5`) parsed as positive (M-5)
- `cfg_strncmp` prefix-matched longer keys (H-5)
- NULL dereference in OLED brightness hook after disable (C-5)
- LUT compounding on repeated screen filter apply (C-4)
- Missing filter re-apply after `vitabrightOledReload` (M-7)
- Userland `is_oled` accepted from untrusted caller in filter syscalls (L-5)
- LCD table scan used wrong segment base, could inject into arbitrary memory (C-3)
- `ref[3] == 0` not guarded in white-point normalisation (H-3)
