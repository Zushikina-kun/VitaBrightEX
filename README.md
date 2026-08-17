# VitaBrightEX

> **Based on [vitabright](https://github.com/devnoname120/vitabright) by [@devnoname120](https://github.com/devnoname120)**

VitaBrightEX is an enhanced fork of vitabright that fixes the OLED red-screen
colour shift, adds a Rosalina-equivalent screen filter pipeline, full PS Vita 2000
(LCD) colour enhancement, automatic per-panel LUT selection, and firmware
compatibility up to 3.74.

**Compatible with: PS Vita 1000 (OLED) and PS Vita 2000 / Slim (LCD)**

---

## Original Project

This plugin is built on top of the original work by the vitabright community:

- **[devnoname120/vitabright](https://github.com/devnoname120/vitabright)** — original plugin, LUT format reverse engineering, OLED DDB detection
- **[SKGleba](https://github.com/SKGleba)** — multi-panel OLED DDB fix (issue [#13](https://github.com/devnoname120/vitabright/issues/13))
- **@buzeak** — improved LUT tables (issues [#36](https://github.com/devnoname120/vitabright/issues/36), [#38](https://github.com/devnoname120/vitabright/issues/38))
- **Community contributors** — LUT research in issues [#40](https://github.com/devnoname120/vitabright/issues/40), [#44](https://github.com/devnoname120/vitabright/issues/44), [#49](https://github.com/devnoname120/vitabright/issues/49)
- **xyz, yifanlu, xerpi** — taiHEN, kernel tooling, initial Vita RE
- **HenriBeyle, Snivy102** — Vita 2000 colour-space registry discovery
- **LumaTeam/Luma3DS** — Rosalina screen filter design and CCT algorithm reference ([source](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/rosalina/source/menus/screen_filters.c), GPLv3)

---

## What's new vs. original vitabright

| Feature | Original | VitaBrightEX |
|---|---|---|
| OLED brightness extension | ✓ | ✓ |
| LCD brightness extension | ✓ | ✓ + user-editable table file |
| Firmware support | 3.60–3.70 | **3.60–3.74+** (NID-based, no hardcoded offsets) |
| OLED red-screen fix | Partial | ✓ Auto white-point normalisation |
| Per-panel LUT auto-selection | ✗ | ✓ Detects AMS495QA01/04 + fallback |
| Balanced LUT per panel | Single LUT | ✓ p4 / p5 / p6 / default |
| Colour bias tuning (R/G/B) | ✗ | ✓ Config file |
| Night / warm mode | ✗ | ✓ Amber tint below brightness threshold |
| **Screen filter (Rosalina-style)** | ✗ | ✓ CCT + gamma + contrast + brightness + invert |
| IPS panel colour curve fix | ✗ | ✓ For Vita 2000 LCD IPS screens |
| LCD colour space enhancement | ✗ | ✓ Full RGB range + wide gamut (live, no reboot) |
| LCD brightness user table | ✗ | ✓ `vitabright_lcd_lut.txt` |
| Config file | ✗ | ✓ `vitabrightex.cfg` |
| Syscall: get panel type | ✗ | ✓ |
| Syscall: filter set/get/reset | ✗ | ✓ |

---

## Installation

1. Download the latest release ZIP and extract it.
2. Copy everything from the `ur0_tai/` folder into `ur0:/tai/` on your Vita.
3. Add to `ur0:/tai/config.txt` under `*KERNEL`:
   ```
   ur0:/tai/vitabright.skprx
   ```
4. Reboot.

> **Note:** If `ux0:/tai/` exists on your memory card it overrides `ur0:/tai/`.
> Either put all files there too, or delete `ux0:/tai/`.

### Release package contents

```
ur0_tai/
  vitabright.skprx            ← kernel plugin
  vitabright_lut.txt          ← OLED default/fallback LUT
  vitabright_lut_p4.txt       ← OLED panel 4 (AMS495QA04)
  vitabright_lut_p5.txt       ← OLED panel 5 (AMS495QA01)
  vitabright_lut_p6.txt       ← OLED replacement/aftermarket panels
  vitabright_lcd_lut.txt      ← Vita 2000 brightness curve (optional)
  vitabrightex.cfg            ← configuration template (optional)
```

---

## OLED — Fixing the red screen at low brightness

The "red screen" issue affects certain PCH-1000/1010/1101 units.
VitaBrightEX fixes this in two ways working together:

### 1. Per-panel LUT auto-selection

The plugin reads `supplier_elective_data` from the OLED's DDB (Device Descriptor Block)
and automatically loads the matching LUT file:

| `supplier_elective_data & 0xFF` | Panel | LUT loaded |
|---|---|---|
| `5` — AMS495QA01 | Most PCH-1000 units | `vitabright_lut_p5.txt` |
| `4` — AMS495QA04 | Some PCH-1000 units | `vitabright_lut_p4.txt` |
| `6` | Replacement/aftermarket panels | `vitabright_lut_p6.txt` |
| other | Unknown/older panels | `vitabright_lut.txt` |

If a panel-specific file is missing it falls back to the next option down.

### 2. Automatic white-point normalisation

After loading the LUT, the plugin normalises the R/B channel ratios across all
brightness rows so they track the G channel evenly as you dim. This prevents the
red sub-pixel from being overdriven at low brightness without requiring a custom LUT.

### 3. Manual colour bias

If you still see a tint, add a bias in `vitabrightex.cfg`:
```ini
color_r_bias = -8    # reduce red
color_b_bias = 4     # boost blue to compensate
```
Typical range: ±4 to ±20. Start small.

---

## Screen Filter — Rosalina-equivalent colour pipeline

Works on **both** Vita 1000 (OLED) and Vita 2000 (LCD).

Inspired by [Luma3DS Rosalina's screen filter system](https://github.com/LumaTeam/Luma3DS),
which writes a 256-entry per-channel LUT to the 3DS GPU hardware registers.

On PS Vita the equivalent mechanisms are:
- **OLED:** the panel's `SET_NORMAL_GAMMA_CONTROL (0xF9)` LUT (17 brightness rows)
- **LCD:** the IFTU 3×3 CSC (Colour Space Conversion) matrix applied to every pixel in hardware

Both are computed from the same Rosalina-style parameters:

| Parameter | Default | Description |
|---|---|---|
| `filter_cct` | `6500` | Colour temperature in Kelvin (1000–25100) |
| `filter_gamma` | `1.0` | Gamma exponent (0.1–8.0) |
| `filter_contrast` | `1.0` | Contrast multiplier (0.0–4.0) |
| `filter_brightness` | `0.0` | Black-level offset (−1.0 to 1.0) |
| `filter_invert` | `0` | Hardware invert (0/1) |
| `filter_panel_enhance` | `0` | IPS curve fix: 0=off, 1=IPS, 2=sRGB |

### Colour temperature presets (set `filter_cct` to any of these)

| CCT (K) | Name |
|---|---|
| 10000 | Aquarium |
| 7500 | Overcast Sky |
| 6500 | Default (neutral D65) |
| 5500 | Daylight |
| 4200 | Fluorescent |
| 3400 | Halogen |
| 2700 | Incandescent |
| 2300 | Warm Incandescent |
| 1900 | Candle |
| 1200 | Ember |

### IPS panel fix (`filter_panel_enhance = 1`)

Corrects the non-linear gamma roll-off of IPS LCD panels (Vita 2000). Applies a
pre-measured sRGB linearisation curve before computing the CSC matrix — equivalent
to Rosalina's `ctrToSrgbTable` correction for IPS 3DS screens.

---

## LCD / Vita 2000 — Colour enhancement

The Vita 2000's LCD defaults to limited-range RGB (16–235) and a narrower gamut
than the Vita 1000 OLED. VitaBrightEX unlocks the SoC's built-in wide-gamut mode:

- **Full RGB range (0–255):** removes the washed-out look on dark scenes
- **Wide colour gamut:** closer to OLED saturation
- **Live application:** no reboot required

Enabled by default via `vitabrightex.cfg`. Set `lcd_color_space_mode = 0` and
`lcd_rgb_range_mode = 0` to restore stock appearance.

---

## Night / Warm Mode (OLED)

Set `night_mode_enabled = 1` in `vitabrightex.cfg` to tint the image amber at
low brightness, reducing blue-light output for comfortable night use.

`night_mode_threshold` controls which brightness level activates it (0–16, default 6).

---

## Full config reference (`vitabrightex.cfg`)

```ini
# OLED (Vita 1000)
oled_panel_lut_override = 0       # 1 = force panel_lut_path instead of auto
panel_lut_path =                  # custom LUT path (when override = 1)
color_r_bias = 0                  # R channel bias −127..127
color_g_bias = 0                  # G channel bias
color_b_bias = 0                  # B channel bias
night_mode_enabled = 0            # 1 = amber tint at low brightness
night_mode_threshold = 6          # brightness level 0–16 that triggers tint
oled_dim_workaround = 1           # 1 = prevent auto-dim raising brightness

# LCD (Vita 2000)
lcd_color_space_mode = 1          # 1 = wide gamut (OLED-like)
lcd_rgb_range_mode = 1            # 1 = full RGB 0–255
lcd_saturation_boost = 1          # 1 = on
lcd_ips_enhance = 1               # 1 = live driver colour-space switch

# Screen filter (both models)
filter_cct = 6500                 # colour temperature K
filter_gamma = 1.0                # gamma exponent
filter_contrast = 1.0             # contrast multiplier
filter_brightness = 0.0           # black-level offset
filter_invert = 0                 # 0 = normal, 1 = inverted
filter_panel_enhance = 0          # 0=off 1=IPS fix 2=sRGB fix
```

---

## OLED gamma table format

17 rows × 21 space-separated hex bytes. Lines starting with `#` are comments.

```
Row 0  = maximum brightness (or extra-bright level)
...
Row 15 = minimum on-slider brightness
Row 16 = inactivity-dim value (not accessible from slider)
```

Use [vitabright-lut-editor](https://github.com/devnoname120/vitabright-lut-editor/releases/latest)
to create and preview custom LUT files visually.

More details: [OLED gamma table wiki](https://github.com/devnoname120/vitabright/wiki/What-is-the-format-of-the-OLED-gamma-table%3F)

---

## Firmware compatibility

VitaBrightEX uses **NID-based function resolution** via `module_get_export_func`
from taihenModuleUtils. This replaces all hardcoded byte offsets from the original
plugin and automatically works on any firmware that exports the same `SceOled` /
`SceLcd` / `ScePower` NIDs — confirmed stable from 3.60 to 3.74.

---

## Building from source

Requires [VitaSDK](https://vitasdk.org/) and [taiHEN](https://github.com/yifanlu/taiHEN/releases).

```sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# With debug logging enabled (writes to ur0:data/vitabright_log.txt):
cmake .. -DENABLE_LOGGING=ON
make
```

Deploy to Vita over FTP:
```sh
make send PSVITAIP=192.168.1.xxx
```

---

## Changes from original vitabright

See [CHANGELOG.md](CHANGELOG.md) for a full list of changes.

---

## Credits

| Contributor | Contribution |
|---|---|
| [devnoname120](https://github.com/devnoname120) | Original vitabright plugin, LUT format RE, DDB detection |
| [SKGleba](https://github.com/SKGleba) | Multi-panel OLED DDB fix |
| [@buzeak](https://github.com/devnoname120/vitabright/issues/36) | Improved gamma tables |
| [LumaTeam](https://github.com/LumaTeam/Luma3DS) | Rosalina screen filter design (CCT algorithm, IPS fix concept) |
| [xyz, yifanlu, xerpi](https://github.com/yifanlu/taiHEN) | taiHEN, kernel RE, Vita toolchain |
| HenriBeyle, Snivy102 | Vita 2000 colour-space registry discovery |
| vitabright community | LUT research and testing (issues #36–#52) |
