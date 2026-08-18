# VitaBrightEX

> **Based on [vitabright](https://github.com/devnoname120/vitabright) by [@devnoname120](https://github.com/devnoname120)**

VitaBrightEX is an enhanced fork of vitabright that:

- Fixes the **OLED red-screen colour shift** at low brightness on affected PCH-1000/1010/1101 units
- Adds full **PS Vita 2000 (LCD) colour enhancement** — full RGB range + wide gamut, live with no reboot
- Auto-selects the correct **per-panel LUT** for every known OLED variant
- Adds a **Rosalina-equivalent screen filter** — colour temperature, gamma, contrast, brightness, hardware invert
- Works on **every firmware version 3.60–3.74+** via NID-based function resolution (no hardcoded offsets)
- Ships a **companion LUT editor app** (VitaBrightEX LUT Editor v2.0) that integrates all features

---

## Downloads

**[Latest release →](https://github.com/Zushikina-kun/VitaBrightEX/releases/latest)**

| File | What it is |
|---|---|
| `VitaBrightEX-v1.2.zip` | Kernel plugin + LUT files + config template |
| `VitaBrightEX-LUT-Editor-v2.0.vpk` | Companion app (install via VitaShell) |

---

## Installation

### Plugin
1. Download `VitaBrightEX-v1.2.zip` and extract it.
2. Copy all files inside to `ur0:/tai/` on your Vita.
3. In `ur0:/tai/config.txt`, add under `*KERNEL`:
   ```
   ur0:/tai/vitabright.skprx
   ```
4. Reboot.

> If `ux0:/tai/` exists on your memory card, it overrides `ur0:/tai/`. Either put the files there instead, or delete `ux0:/tai/` to use the `ur0` path.

### LUT Editor companion app
1. Transfer `VitaBrightEX-LUT-Editor-v2.0.vpk` to your Vita.
2. Install via VitaShell.
3. Launch from LiveArea. The plugin must be running for the editor to work.

### Release ZIP contents

| File | Purpose |
|---|---|
| `vitabright.skprx` | Kernel plugin — the main file to install |
| `vitabright_lut.txt` | OLED default/fallback gamma LUT |
| `vitabright_lut_p4.txt` | OLED panel 4 (AMS495QA04) — balanced LUT |
| `vitabright_lut_p5.txt` | OLED panel 5 (AMS495QA01) — balanced LUT |
| `vitabright_lut_p6.txt` | OLED replacement/aftermarket panels — balanced LUT |
| `vitabright_lcd_lut.txt` | Vita 2000 brightness curve (optional, edit to customise) |
| `vitabrightex.cfg` | Config template (optional — safe defaults used if absent) |

---

## What's new vs. original vitabright

| Feature | Original vitabright | VitaBrightEX |
|---|---|---|
| OLED brightness extension | ✓ | ✓ |
| LCD brightness extension | ✓ | ✓ + user-editable brightness curve file |
| Firmware support | 3.60–3.70 | **3.60–3.74+** (NID-based, no hardcoded offsets) |
| OLED red-screen fix | Partial | ✓ Auto white-point normalisation |
| Per-panel LUT auto-selection | ✗ | ✓ Reads DDB, loads matching file automatically |
| Balanced LUT per panel | Single file | ✓ p4 / p5 / p6 / default — separate tuned files |
| Colour bias tuning (R/G/B) | ✗ | ✓ Config file |
| Night / warm mode | ✗ | ✓ Amber tint below configurable brightness threshold |
| Screen filter (Rosalina-equivalent) | ✗ | ✓ CCT + gamma + contrast + brightness + invert |
| IPS panel colour curve fix | ✗ | ✓ For Vita 2000 LCD |
| LCD colour space enhancement | ✗ | ✓ Full RGB range + wide gamut, live (no reboot) |
| LCD brightness curve file | ✗ | ✓ `vitabright_lcd_lut.txt` |
| Config file | ✗ | ✓ `vitabrightex.cfg` |
| LUT editor companion app | Original crashes | ✓ Fixed + extended with filter/panel/LCD support |

---

## Fixing the OLED red screen at low brightness

The red-screen issue affects PCH-1000/1010/1101 units where the red sub-pixel is
over-represented in the firmware's gamma table at low brightness levels.

VitaBrightEX fixes this in layers:

### 1. Per-panel LUT auto-selection

At startup the plugin reads `supplier_elective_data` from the OLED's Device Descriptor
Block (DDB) and automatically loads the matching balanced LUT file:

| `supplier_elective_data & 0xFF` | Panel | LUT file |
|---|---|---|
| `5` (AMS495QA01) | Most PCH-1000 units | `vitabright_lut_p5.txt` |
| `4` (AMS495QA04) | Some PCH-1000 units | `vitabright_lut_p4.txt` |
| `6` | Replacement / aftermarket OLED | `vitabright_lut_p6.txt` |
| other / unknown | Older or unrecognised panels | `vitabright_lut.txt` |

If a panel-specific file is missing, it falls back to the next option.

### 2. White-point normalisation

After loading the LUT, the plugin normalises the R/B channel ratios against the
G channel anchor across all brightness rows. This prevents the red sub-pixel from
being overdriven at low brightness without requiring a custom-tuned LUT.

### 3. Manual colour bias (if a tint remains)

Add to `vitabrightex.cfg`:
```ini
color_r_bias = -8
color_b_bias = 4
```
Typical range: ±4 to ±20. Start with small values and adjust.

---

## Screen filter — Rosalina-equivalent colour pipeline

Works on **both Vita 1000 (OLED) and Vita 2000 (LCD)**.

Inspired by [Luma3DS Rosalina's screen filter system](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/rosalina/source/menus/screen_filters.c).

**How it works on each model:**
- **OLED:** the filter is baked into the 17-row panel gamma LUT before injection
- **LCD:** the filter is written as a 3×3 IFTU CSC (Colour Space Conversion) matrix applied in hardware to every pixel before it reaches the panel

**Important:** The filter is bypassed entirely when all parameters are at their defaults
(`filter_cct=6500`, `filter_gamma=1.0`, `filter_contrast=1.0`, `filter_brightness=0.0`,
`filter_invert=0`, `filter_panel_enhance=0`). Setting any parameter away from its
default activates the filter path.

### Parameters

| Parameter | Default | Range | Description |
|---|---|---|---|
| `filter_cct` | `6500` | 1000–25100 | Colour temperature in Kelvin |
| `filter_gamma` | `1.0` | 0.1–8.0 | Gamma exponent |
| `filter_contrast` | `1.0` | 0.0–4.0 | Contrast multiplier |
| `filter_brightness` | `0.0` | −1.0–1.0 | Black-level offset |
| `filter_invert` | `0` | 0/1 | Hardware colour invert |
| `filter_panel_enhance` | `0` | 0/1/2 | Panel linearisation (0=off, 1=IPS, 2=sRGB) |

### Colour temperature presets

| `filter_cct` value | Name |
|---|---|
| 10000 | Aquarium (very cool/blue) |
| 7500 | Overcast Sky |
| 6500 | Default neutral |
| 5500 | Daylight |
| 4200 | Fluorescent |
| 3400 | Halogen |
| 2700 | Incandescent (warm) |
| 2300 | Warm Incandescent |
| 1900 | Candle |
| 1200 | Ember (very warm/amber) |

### IPS panel enhancement (`filter_panel_enhance = 1`)

Applies a measured sRGB linearisation curve to correct the non-linear gamma
roll-off of IPS LCD panels (Vita 2000). This is the direct equivalent of
Rosalina's **"[IPS recommended] Enhance screen colors"** option.

---

## Vita 2000 (LCD) colour enhancement

The Vita 2000 LCD defaults to limited-range RGB (16–235) and a narrower colour
gamut than the Vita 1000 OLED. VitaBrightEX activates the SoC's wide-gamut mode:

- **`lcd_color_space_mode = 1`** — writes `color_space_mode=1` to the `/CONFIG/DISPLAY`
  registry (wider gamut, closer to OLED appearance)
- **`lcd_rgb_range_mode = 1`** — writes `rgb_range_mode=1` to `/CONFIG/DISPLAY`
  (full RGB 0–255 instead of limited 16–235)
- **`lcd_ips_enhance = 1`** — calls `sceLcdSetDisplayColorSpaceModeForDriver` live
  (takes effect without a reboot)
- **`lcd_saturation_boost = 1`** — alias for `lcd_ips_enhance`; either one enables
  the live driver colour-space switch

All four default to `1` (enabled). Set to `0` in `vitabrightex.cfg` to disable.

### Custom LCD brightness curve

Edit `vitabright_lcd_lut.txt` — 17 decimal values (0–255), one per line, from
brightest (line 1) to dimmest (line 17). Copy to `ur0:/tai/` to activate.

---

## Night / warm mode (OLED)

Tints the image amber at low brightness to reduce blue-light output for night use.

```ini
night_mode_enabled = 1
night_mode_threshold = 6   # row 0–16; higher = activates at brighter levels
```

---

## LUT Editor companion app — controls

| Button | Action |
|---|---|
| Triangle / Square | Previous / next LUT row (0 = max, 16 = inactivity dim) |
| Left / Right | Move byte cursor within the row |
| L1 / R1 | Jump cursor by 7 bytes (next colour group) |
| Up / Down | Increase / decrease byte value **or** filter parameter (when L2 active) |
| L2 | Cycle filter edit mode: LUT → CCT → Gamma → Contrast → Brightness |
| R2 (held) | 10× faster adjustment for filter values |
| Cross | Cycle test image (colorbars / matrix / TV) |
| Circle | Reload LUT and filter settings from disk |
| Select | Set screen brightness to the currently displayed LUT row |
| Start | Save LUT to file + write filter params to `vitabrightex.cfg` |

The app displays the detected panel name (e.g. AMS495QA01) and the current screen
filter parameters at the bottom of the screen.

On **Vita 2000**, the LUT editor shows LCD colour enhancement status and allows
screen filter editing instead of the OLED LUT byte editor.

---

## Full config reference (`vitabrightex.cfg`)

Copy to `ur0:/tai/vitabrightex.cfg`. All keys are optional — safe defaults apply
when the file is absent.

```ini
# ============================================================
# OLED (PS Vita 1000)
# ============================================================

# 1 = load panel_lut_path instead of auto-detecting from DDB
oled_panel_lut_override = 0

# Custom LUT path (used only when oled_panel_lut_override = 1)
# panel_lut_path = ur0:/tai/my_lut.txt

# Per-channel colour bias (−127 to 127)
# Positive = boost, negative = reduce
color_r_bias = 0
color_g_bias = 0
color_b_bias = 0

# Night/warm mode — amber tint below brightness threshold
night_mode_enabled = 0
night_mode_threshold = 6   # 0–16; default 6 = bottom ~3 slider positions

# Prevent auto-dim from paradoxically raising brightness at very low levels
oled_dim_workaround = 1

# ============================================================
# LCD (PS Vita 2000)
# ============================================================

# Wide colour gamut — registry: /CONFIG/DISPLAY/color_space_mode
lcd_color_space_mode = 1

# Full RGB range (0–255) — registry: /CONFIG/DISPLAY/rgb_range_mode
lcd_rgb_range_mode = 1

# Live colour-space switch via SceLcd driver (no reboot needed)
lcd_ips_enhance = 1

# Alias for lcd_ips_enhance — setting either one to 1 enables the live switch
lcd_saturation_boost = 1

# ============================================================
# Screen filter (both models — bypassed when all at defaults)
# ============================================================

# Colour temperature in Kelvin (1000–25100)
filter_cct = 6500

# Gamma exponent (0.1–8.0) — >1.0 = darker midtones, <1.0 = brighter
filter_gamma = 1.0

# Contrast multiplier (0.0–4.0)
filter_contrast = 1.0

# Black-level offset (−1.0–1.0) — positive = raised blacks (washed out)
filter_brightness = 0.0

# Hardware colour invert (0 = normal, 1 = inverted)
filter_invert = 0

# Panel colour curve correction
# 0 = off  |  1 = IPS linearisation  |  2 = sRGB linearisation
filter_panel_enhance = 0
```

---

## OLED gamma table format

17 rows × 21 space-separated uppercase hex bytes. Lines starting with `#` are comments.

```
Row 0  = maximum brightness (or extra-bright above stock)
Row 1  = second extra-bright level
Rows 2–15 = stock brightness slider range (bright → dim)
Row 16 = inactivity-dim sentinel (not on slider)
```

The 21 bytes per row map to the OLED panel's `SET_NORMAL_GAMMA_CONTROL (0xF9)` command.
See the [original vitabright wiki](https://github.com/devnoname120/vitabright/wiki/What-is-the-format-of-the-OLED-gamma-table%3F)
for the detailed byte layout.

Use the **VitaBrightEX LUT Editor** to edit and preview LUT files on-device, or
transfer files over FTP and reload them with Circle in the editor.

---

## Firmware compatibility

VitaBrightEX uses `module_get_export_func()` from taihenModuleUtils to resolve all
`SceOled`, `SceLcd`, `ScePower`, `SceDisplay`, and `SceRegMgr` functions by NID.
No hardcoded byte offsets — the plugin automatically adapts to any firmware version
that exports the same NIDs, confirmed stable from 3.60 to 3.74.

---

## Building from source

### Requirements
- [VitaSDK](https://vitasdk.org/) with ARM cross-compiler
- [taiHEN](https://github.com/yifanlu/taiHEN/releases) headers + stubs

```sh
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

# Build plugin
cd VitaBrightEX
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -G "Unix Makefiles"
make

# Build LUT editor (requires libvita2d + portlibs also built)
cd ../../vitabright-lut-editor
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -G "Unix Makefiles"
make
```

Enable debug logging (writes to `ur0:data/vitabright_log.txt`):
```sh
cmake .. -DENABLE_LOGGING=ON
```

Deploy plugin + all files to Vita over FTP:
```sh
make send PSVITAIP=192.168.1.xxx
```

---

## Credits

| Contributor | Contribution |
|---|---|
| [devnoname120](https://github.com/devnoname120) | Original vitabright plugin and LUT editor, LUT format RE, DDB detection |
| [SKGleba](https://github.com/SKGleba) | Multi-panel OLED DDB fix |
| [@buzeak](https://github.com/devnoname120/vitabright/issues/36) | Improved gamma tables |
| [LumaTeam / Luma3DS](https://github.com/LumaTeam/Luma3DS) | Rosalina screen filter design (CCT algorithm, IPS fix concept) — GPLv3 |
| [xyz, yifanlu, xerpi](https://github.com/yifanlu/taiHEN) | taiHEN, kernel RE, Vita toolchain, libvita2d |
| HenriBeyle, Snivy102 | Vita 2000 colour-space registry discovery |
| vitabright community | LUT research and testing (issues [#36](https://github.com/devnoname120/vitabright/issues/36)–[#52](https://github.com/devnoname120/vitabright/issues/52)) |
