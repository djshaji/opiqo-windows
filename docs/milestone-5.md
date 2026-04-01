# Milestone 5 — MainWindow Layout and 2×2 Plugin Slot Grid

## Changed files

### `src/win32/resource.h`
Three new control IDs added for the expanded `ControlBar`:

| Identifier | Value | Purpose |
|---|---|---|
| `IDC_GAIN_SLIDER` | `50101` | Gain trackbar child control |
| `IDC_RECORD_TOGGLE` | `50102` | Record latching pushbutton |
| `IDC_FORMAT_COMBO` | `50103` | WAV/MP3/OGG format combobox |

### `src/win32/ControlBar.h`
New private members:

| Member | Purpose |
|---|---|
| `gainSlider_` | `HWND` for the `TRACKBAR_CLASS` gain control |
| `recordButton_` | `HWND` for the Record toggle button |
| `formatCombo_` | `HWND` for the format `COMBOBOX` |

New public methods:

| Method | Description |
|---|---|
| `setRecordState(bool)` | Sets Record button checked state via `BM_SETCHECK`, no `WM_COMMAND` side-effect |
| `gainValue()` | Returns current trackbar position in [0, 100] |
| `formatIndex()` | Returns currently selected combo index (0=WAV, 1=MP3, 2=OGG) |
| `setFormatIndex(int)` | Selects a combo entry by index |
| `resize(const RECT&)` | Repositions and resizes the container via `MoveWindow`; called by `doLayout()` on every `WM_SIZE` |

### `src/win32/ControlBar.cpp`
- Added `#include <commctrl.h>` for `TRACKBAR_CLASSA`.
- `create()` now builds four child controls inside the container panel:
  - **Power** (`BS_AUTOCHECKBOX | BS_PUSHLIKE`, 80×28 px at offset 4,4) — unchanged from Milestone 4.
  - **Gain** (`TRACKBAR_CLASSA | TBS_HORZ | TBS_NOTICKS`, 120×20 px at 92,8); range 0–100, default position 80.
  - **Record** (`BS_AUTOCHECKBOX | BS_PUSHLIKE`, 80×28 px at 220,4).
  - **Format** (`CBS_DROPDOWNLIST`, 100×120 px at 308,4); pre-populated with "WAV", "MP3", "OGG"; default selection 0 (WAV).
- `resize()` implemented via `MoveWindow` on the container `hwnd_`.

### `src/win32/PluginSlot.h`
- Window style upgraded: `WS_EX_CLIENTEDGE` added (sunken framed border).
- Default label text changed from `"Plugin Slot"` to `"Empty Slot"`.
- Added `setLabel(const char*)` — updates the `STATIC` text via `SetWindowTextA`; called by Milestone 6 to reflect active plugin name.
- Added `resize(const RECT&)` — repositions and resizes via `MoveWindow`; called by `doLayout()`.

### `src/win32/MainWindow.h`
- Added `#include "PluginSlot.h"`.
- Added `statusBar_` (`HWND`) member for the `STATUSCLASSNAME` window.
- Added `slots_[4]` (`PluginSlot`) member array for the 2×2 grid.
- Added `doLayout()` private method declaration.

### `src/win32/MainWindow.cpp`

#### File-level constants (new)
```cpp
static constexpr int kBarHeight    = 40;   // ControlBar height
static constexpr int kStatusHeight = 22;   // Status bar fallback height
static constexpr int kMinWidth     = 900;
static constexpr int kMinHeight    = 650;
```
Added `#include <commctrl.h>` for `STATUSCLASSNAME`.

#### `create()` additions
1. **Status bar** created with `STATUSCLASSNAME | SBARS_SIZEGRIP` immediately after the window is made. Two parts defined: part 0 width 300 px (input), part 1 extends to window edge (output). Initial text set to `"In: (none)"` and `"Out: (none)"`.
2. **ControlBar and slots** created with a zero-size `RECT` placeholder — actual bounds applied by the first `doLayout()` call.
3. Each slot labelled `"Slot 1"` through `"Slot 4"` via `setLabel()`.
4. `doLayout()` called before `ShowWindow` to apply correct initial geometry.

#### `doLayout()` (new method)
Computes child-window geometry from the current client rect:

```
[status bar]          — self-sized row at top
[Slot 1] [Slot 2]     — upper half of remaining area
[Slot 3] [Slot 4]     — lower half of remaining area
[ControlBar]          — kBarHeight row at bottom
```

Steps:
1. Sends `WM_SIZE` to `statusBar_` for self-sizing; reads back its actual pixel height via `GetWindowRect`.
2. Computes `slotAreaTop = sbH`, `slotAreaBot = totalH − kBarHeight`.
3. Splits the slot area into four equal quadrants (`halfW × halfH` each).
4. Calls `slots_[i].resize(slotBounds[i])` for all four slots.
5. Calls `controlBar_.resize(barBounds)` for the bottom strip.

#### `onDeviceListChanged()` additions
After resolving device IDs, walks the input and output `DeviceInfo` lists to find the matching `friendlyName` for each resolved ID and updates the two status bar parts:
- Part 0: `"In: <friendly name>"` or `"In: (none)"`.
- Part 1: `"Out: <friendly name>"` or `"Out: (none)"`.

#### New `WM_SIZE` handler
Calls `doLayout()` on every resize; preserves slot proportions at all window sizes.

#### New `WM_GETMINMAXINFO` handler
Constrains `ptMinTrackSize` to `{kMinWidth, kMinHeight}` = `{900, 650}`, enforcing the minimum usable window size.
