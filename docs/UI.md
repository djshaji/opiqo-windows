# UI Layout and Structure

## Window Hierarchy

```
MainWindow  (WS_OVERLAPPEDWINDOW, 900×650 min logical px, opens maximised)
 ├── [Menu bar]  (IDR_MAINMENU from app.rc)
 ├── PluginSlot[0]  (top-left quadrant)
 ├── PluginSlot[1]  (top-right quadrant)
 ├── PluginSlot[2]  (bottom-left quadrant)
 ├── PluginSlot[3]  (bottom-right quadrant)
 ├── ControlBar  (40 logical-px strip above status bar, DPI-scaled at runtime)
 └── STATUSCLASSNAME  (≈22 px, docked to bottom)
```

---

## DPI Scaling

The process declares **per-monitor v2 DPI awareness** via
`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`,
called in `WinMain` before any window is created. The function is loaded
dynamically so the binary still runs on older Windows versions (where it
falls back to unaware virtualisation).

All hardcoded pixel values throughout the code are **logical 96-dpi pixels**.
They are scaled to physical pixels at runtime using
`MulDiv(px, GetDpiForWindow(hwnd), 96)` at the point of each `MoveWindow` /
`CreateWindow` call.

When the window moves to a monitor with a different DPI, `WM_DPICHANGED` is
handled in `MainWindow`:
1. `SetWindowPos` applies the OS-suggested physical rect for the new DPI.
2. Parameter panels for any loaded plugins are rebuilt (child control
   positions are baked into the `ParameterPanel::build()` call).
3. `doLayout()` recalculates all zone bounds at the new DPI.

`WM_GETMINMAXINFO` scales the `kMinWidth` / `kMinHeight` logical constants to
physical pixels for the current monitor before returning the min-track size.

---

## Layout Zones (`doLayout()`)

| Zone | Height | Position |
|---|---|---|
| **Slot grid** | `clientH − barH − statusH`  (`barH` = `kBarHeight` scaled to current DPI) | top of client area |
| **ControlBar** | `kBarHeight` (40 logical px) scaled to current DPI | immediately above status bar |
| **Status bar** | theme/DPI-sized (≈22 px) | docked to bottom |

The 2×2 slot grid splits the remaining space evenly: each slot gets
`(clientW / 2) × (slotAreaH / 2)`. There is no minimum per-slot size guard
beyond the window minimum.

---

## ControlBar (40 logical px, left-to-right, logical pixel offsets scaled at runtime)

| x (logical px) | Width (logical px) | Control |
|---|---|---|
| 4 | 80 | **Power** — `BS_AUTOCHECKBOX\|BS_PUSHLIKE` (latching toggle) |
| 92 | 120 | **Gain** — `TRACKBAR` 0–100, no ticks |
| 220 | 80 | **Record** — `BS_AUTOCHECKBOX\|BS_PUSHLIKE` (latching toggle) |
| 308 | 100 | **Format** — `COMBOBOX` (WAV / MP3 / OGG) |
| 416 | 90 | **Quality** — `COMBOBOX` (High / Medium / Low); hidden when WAV selected |

All offsets are logical 96-dpi pixels. Both `create()` and `resize()` apply
`MulDiv(offset, GetDpiForWindow(hwnd_), 96)` before every `CreateWindow` /
`MoveWindow` call, so controls are correctly sized and positioned on any
monitor DPI.

---

## PluginSlot (custom class `OpiqoPluginSlot`, `WS_EX_CLIENTEDGE`)

Each of the four slots contains:

| y (logical px) | Control |
|---|---|
| 4 | **Label** `STATIC` — shows slot number or active plugin name |
| 30 | **Add Plugin** button (always enabled) |
| 30 | **Bypass** button (disabled when slot is empty) |
| 30 | **Delete** button (disabled when slot is empty) |
| below buttons (y = 60 logical px) | **ParameterPanel** — built dynamically when a plugin is loaded |

All y-offsets and widths are logical 96-dpi pixels scaled via
`MulDiv(..., GetDpiForWindow(hwnd_), 96)` in both `resize()` and
`buildParameterPanel()`.

`WM_COMMAND` from child buttons is forwarded to `MainWindow` via `GetParent()`.

`hasPlugin()` returns `true` when a plugin is loaded (used by `WM_DPICHANGED`
to determine which panels to rebuild).

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

Layout constants (logical 96-dpi px, scaled to physical px at `build()` time):

| Constant | Logical value | Meaning |
|---|---|---|
| `kRowH` | 28 px | Height of each control row |
| `kLabelW` | 130 px | Width of the label column |
| `kCtrlW` | 200 px | Default width of the control widget |
| `kPadX` | 4 px | Horizontal padding |
| `kPadY` | 4 px | Vertical gap between rows |

`build()` calls `GetDpiForWindow` once and scales all five constants via
`MulDiv` before creating any child controls. The resulting scaled row height
is stored in `rowH_` and used by `onVScroll` / `onMouseWheel` for the
scroll step, so scrolling also scales correctly.

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

## Fonts

All controls use a shared **10pt Segoe UI** font created by
`MainWindow::rebuildUiFont()` via `CreateFontA` with a point-to-pixel
conversion that accounts for the current monitor DPI:

```
height = -MulDiv(10, GetDpiForWindow(hwnd_), 72)
```

The font handle is stored in `MainWindow::uiFont_` and pushed to every
descendant child window by `MainWindow::applyUiFont()` using
`EnumChildWindows` + `WM_SETFONT`.

`applyUiFont()` is called:
- After initial window creation
- After every `buildParameterPanel()` call (Add button, import preset, stress test)
- After `WM_DPICHANGED` (alongside `rebuildUiFont()` to re-create the
  font at the new DPI before pushing it)

`PluginDialog::showModal()` accepts an optional `HFONT` parameter and applies
it to the listbox, OK, and Cancel controls in `WM_CREATE`.

The Settings dialog (`IDD_SETTINGS` in `app.rc`) uses dialog units with
`10, "MS Shell Dlg"`, which Windows maps to Segoe UI on Vista and later.

---

## Known Structural Issues

1. **ControlBar absolute offsets** — controls overlap or clip when the bar is
   narrower than ≈520 px; there is no reflow or minimum-width enforcement.
   In practice this is prevented by the 900 px minimum window width.

2. ~~**No DPI awareness**~~ — **Fixed.** Process declares
   `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` in `WinMain`; all pixel
   constants are now scaled via `MulDiv(px, GetDpiForWindow(hwnd), 96)`
   at creation/resize time; `WM_DPICHANGED` triggers panel rebuild,
   font rebuild, and re-layout; `WM_GETMINMAXINFO` scales min-track to
   physical pixels.

3. **PluginSlot header controls are not responsive** — `Add / Bypass / Delete`
   buttons are at hardcoded y = 30 with fixed widths and do not resize with
   the slot. In practice prevented by the 900 px minimum window width.

4. **Zero-height slot guard** — `doLayout()` clamps `slotAreaBot` to
   `slotAreaTop` but does not propagate a minimum height to each slot; very
   small windows produce zero-height (invisible) slots without crashing.
   In practice prevented by the minimum window height constraint.

5. ~~**`ControlBar::resize()` does not reposition proportionally**~~ —
   **Fixed as part of the DPI work.** Both `create()` and `resize()` now
   apply `MulDiv` scaling; the earlier prose claiming proportional reflow
   was incorrect and has been updated accordingly.

6. **Integer division in the 2×2 slot grid** — `doLayout()` computes each
   slot's dimensions as `totalW / 2` and `slotAreaH / 2` using truncating
   integer division. On odd client widths or heights the right/bottom edge
   of the grid is 1 px short, leaving an uncovered strip against the
   ControlBar or the right edge.

7. **ParameterPanel `resize()` is a no-op before a plugin is loaded** —
   `ParameterPanel::hwnd_` is `nullptr` until `build()` is called by
   `PluginSlot::buildParameterPanel()`. Any `WM_SIZE`-triggered `resize()`
   that arrives before a plugin is added silently returns without setting
   the panel bounds, so the first `resize()` after `build()` must establish
   the correct geometry explicitly (which `buildParameterPanel()` does via
   `GetClientRect`).

8. ~~**Controls use the system default bitmap font (tiny)**~~ — **Fixed.**
   `MainWindow` creates a 10pt Segoe UI font via `CreateFontA` scaled to the
   current monitor DPI and pushes it to all child windows with
   `EnumChildWindows` + `WM_SETFONT`. The font is recreated on
   `WM_DPICHANGED` and re-applied after every `buildParameterPanel()` call.
   The Settings dialog font was also bumped from 8pt to 10pt in `app.rc`.
