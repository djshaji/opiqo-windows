# UI Layout and Structure

## Window Hierarchy

```
MainWindow  (WS_OVERLAPPEDWINDOW, 900×650 min, opens maximised)
 ├── [Menu bar]  (IDR_MAINMENU from app.rc)
 ├── PluginSlot[0]  (top-left quadrant)
 ├── PluginSlot[1]  (top-right quadrant)
 ├── PluginSlot[2]  (bottom-left quadrant)
 ├── PluginSlot[3]  (bottom-right quadrant)
 ├── ControlBar  (40 px strip above status bar)
 └── STATUSCLASSNAME  (≈22 px, docked to bottom)
```

---

## Layout Zones (`doLayout()`)

| Zone | Height | Position |
|---|---|---|
| **Slot grid** | `clientH − 40 − statusH` | top of client area |
| **ControlBar** | 40 px fixed | immediately above status bar |
| **Status bar** | theme/DPI-sized (≈22 px) | docked to bottom |

The 2×2 slot grid splits the remaining space evenly: each slot gets
`(clientW / 2) × (slotAreaH / 2)`. There is no minimum per-slot size guard
beyond the window minimum.

---

## ControlBar (40 px, left-to-right, hardcoded pixel offsets)

| x (px) | Width (px) | Control |
|---|---|---|
| 4 | 80 | **Power** — `BS_AUTOCHECKBOX\|BS_PUSHLIKE` (latching toggle) |
| 92 | 120 | **Gain** — `TRACKBAR` 0–100, no ticks |
| 220 | 80 | **Record** — `BS_AUTOCHECKBOX\|BS_PUSHLIKE` (latching toggle) |
| 308 | 100 | **Format** — `COMBOBOX` (WAV / MP3 / OGG) |
| 416 | 90 | **Quality** — `COMBOBOX` (High / Medium / Low); hidden when WAV selected |

Controls use absolute pixel offsets. `resize()` repositions them proportionally
when the window is resized.

---

## PluginSlot (custom class `OpiqoPluginSlot`, `WS_EX_CLIENTEDGE`)

Each of the four slots contains:

| y (px) | Control |
|---|---|
| 4 | **Label** `STATIC` — shows slot number or active plugin name |
| 30 | **Add Plugin** button (always enabled) |
| 30 | **Bypass** button (disabled when slot is empty) |
| 30 | **Delete** button (disabled when slot is empty) |
| below buttons | **ParameterPanel** — built dynamically when a plugin is loaded |

`WM_COMMAND` from child buttons is forwarded to `MainWindow` via `GetParent()`.

---

## ParameterPanel (custom class `OpiqoParamPanel`, scrollable)

Built lazily when `addPlugin()` succeeds. One row per LV2 control-input port:

| LV2 port type | Win32 control |
|---|---|
| `Float` (plain) | `TRACKBAR` (0–10 000 mapped to plugin min–max) |
| `Float` (enumeration) | `COMBOBOX` with scale-point labels |
| `Toggle` | `CHECKBOX` (`BS_AUTOCHECKBOX`) |
| `Trigger` | `BUTTON` — fires `setValue(1.0)` on click |
| `AtomFilePath` | `BUTTON` — opens `GetOpenFileNameA` dialog |

Layout constants:

| Constant | Value | Meaning |
|---|---|---|
| `kRowH` | 28 px | Height of each control row |
| `kLabelW` | 130 px | Width of the label column |
| `kCtrlW` | 200 px | Default width of the control widget |
| `kPadX` | 4 px | Horizontal padding |
| `kPadY` | 4 px | Vertical gap between rows |

The panel is scrollable (`WM_VSCROLL` / `WM_MOUSEWHEEL`) so slots with many
parameters overflow cleanly.

---

## Status Bar

Two parts rendered by `STATUSCLASSNAME`:

| Part | Content |
|---|---|
| Left (half window width) | `In: <input device name>` |
| Right (stretches to edge) | `Out: <output device name>` |

Updated on device arrival/removal via `WM_OPIQO_DEVICE_CHANGE` (`WM_APP + 1`),
which is posted from the COM notification thread and handled on the UI thread.

---

## Known Structural Issues

1. **ControlBar absolute offsets** — controls overlap or clip when the bar is
   narrower than ≈520 px; there is no reflow or minimum-width enforcement.

2. **No DPI awareness** — all pixel constants (`kBarHeight`, `kRowH`,
   `kLabelW`, etc.) are fixed physical pixels with no scaling for high-DPI
   displays.

3. **PluginSlot header controls are not responsive** — `Add / Bypass / Delete`
   buttons are at hardcoded y = 30 with fixed widths and do not resize with
   the slot.

4. **Zero-height slot guard** — `doLayout()` clamps `slotAreaBot` to
   `slotAreaTop` but does not propagate a minimum height to each slot; very
   small windows produce zero-height (invisible) slots without crashing.
