# Changelog

All notable changes from the original [vitabright](https://github.com/devnoname120/vitabright) are documented here.

## VitaBrightEX — Initial Release

### Added

**Core — Firmware compatibility**
- Replaced all hardcoded byte offsets with `module_get_export_func()` NID-based
  resolution from taihenModuleUtils. Plugin now works on firmware 3.60–3.74+
  without version-switch tables for function addresses.

**OLED (Vita 1000)**
- `oled/luts/vitabright_lut_p4.txt` — balanced LUT for AMS495QA04 panels
- `oled/luts/vitabright_lut_p5.txt` — balanced LUT for AMS495QA01 panels
- `oled/luts/vitabright_lut_p6.txt` — balanced LUT for replacement/aftermarket panels
- Auto panel-type detection via `sceOledGetDDB` NID; correct LUT loaded automatically
- White-point normalisation: R/B channels are tracked against the G anchor across
  all brightness rows, preventing the red-screen effect on affected PCH-1010/1101 units
- Per-channel colour bias (`color_r/g/b_bias`) for manual tint correction
- Night/warm mode: amber tint applied below a configurable brightness threshold

**LCD (Vita 2000)**
- User-editable brightness curve via `vitabright_lcd_lut.txt` (17 decimal values)
- Colour space enhancement: `color_space_mode` + `rgb_range_mode` registry writes
  for full RGB range and wider gamut (IPS-style enhancement)
- Live colour-space switch via `sceLcdSetDisplayColorSpaceModeForDriver` — no reboot
- Fixed `lcd_brightness_to_index` to use nearest-neighbour reverse lookup against the
  actual brightness table rather than incorrect linear 0–65536 mapping
- Removed unsafe table-scan heuristic that could write to arbitrary kernel memory;
  replaced with clean versioned offset table

**Screen filter (both models) — Rosalina-equivalent**
- CCT (colour temperature) control: 1000K–25100K via Planckian locus approximation
- Gamma, contrast, brightness controls with fixed-point arithmetic (no libm)
- Hardware invert via `sceDisplaySetInvertColorsForDriver`
- IPS panel colour curve correction (`filter_panel_enhance = 1`) using a measured
  sRGB linearisation table, equivalent to Luma3DS Rosalina's `ctrToSrgbTable`
- OLED: filter baked into the 17-row panel LUT; `lookupBase[]` snapshot prevents
  filter compounding on repeated apply calls
- LCD: filter written as IFTU 3×3 CSC matrix for per-channel hardware correction

**Config system**
- New `vitabrightex.cfg` file read from `ur0:/tai/` or `ux0:/tai/`
- All options optional; safe defaults used when file is absent
- New syscall exports: `vitabrightOledReload`, `vitabrightOledGetPanelType`,
  `vitabrightLcdGetBrightnessValues`, `vitabrightLcdSetBrightnessValues`,
  `vitabrightLcdReapplyColor`, `vitabrightFilterGetParams`,
  `vitabrightFilterSetParams`, `vitabrightFilterReset`

### Fixed (bugs in original vitabright)

- **Infinite loop risk** in config parser EOF detection — now uses return-code
  based termination without consuming extra bytes
- **Kernel hang** in screen filter fixed-point math — `fp_pow` now clamps the
  exponent product to `[-8, 8]` before calling `fp_exp`, preventing thousands of
  loop iterations for very dark LUT rows with high gamma
- **Incorrect `fp_to_u8`** — spurious `>>8` shift made filter output near-zero;
  correct conversion is `(v * 255) >> 16` directly from 16.16 format
- **G channel not filtered** in OLED CCT correction — expanded loop to cover bytes
  3–5 (green midpoints) with the correct white-point index
- **Negative float parse bug** — values like `filter_brightness = -0.5` were read
  as `+0.5` due to `intpart < 0` being false for negative zero; fixed with dedicated
  `cfg_atof()` that checks the sign character independently
- **Prefix key match** in config parser — `cfg_strncmp` would match `night_mode_enabled`
  against `night_mode_enabledXXX`; replaced with exact `cfg_streq`
- **NULL dereference race** in OLED brightness hook — `ksceOledGetBrightness` pointer
  is now NULLed before releasing the hook handle, and the hook body guards for NULL
- **LUT compounding** in screen filter — `apply_filter_to_oled_lut` now copies
  `lookupBase → lookupNew` before each apply so repeated calls don't drift
- **Missing filter re-apply** after `vitabrightOledReload` — screen filter config
  is now reloaded and reapplied alongside the LUT
- **Userland `is_oled` trust** in `vitabrightFilterSetParams` — now uses kernel-side
  `g_is_oled` set at boot instead of accepting the caller's value

### Changed

- `CMakeLists.txt` minimum version bumped to 3.5 for compatibility with CMake 4.x
- `module.yml` version bumped to 3.0 with all new syscall exports listed
- `oled/luts/vitabright_lut.txt` replaced with improved balanced default LUT
- `oled/parser.c` now has full fallback chain: panel-specific → generic → error;
  CR/LF tolerant for Windows-encoded files
