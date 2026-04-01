# Milestone 7 — Dynamic Parameter Panels

## Status: Implemented

---

## What was implemented

### `src/LV2Plugin.hpp` — public `PortInfo` struct and `getControlPortInfo()`

A new public `PortInfo` struct was added to `LV2Plugin`:

| Field | Type | Description |
|---|---|---|
| `portIndex` | `uint32_t` | LV2 port index, used as the `index` argument to `setValue()` |
| `symbol` | `string` | LV2 port symbol |
| `label` | `string` | Human-readable port name (falls back to symbol if empty) |
| `type` | `ControlType` enum | `Float`, `Toggle`, `Trigger`, or `AtomFilePath` |
| `minVal` / `maxVal` / `defVal` | `float` | Port range; degenerate ranges (`max <= min`) are clamped to `[min, min+1]` |
| `currentVal` | `float` | Live value at the time of query |
| `isEnum` | `bool` | True if port carries `lv2:enumeration` or has scale points |
| `scalePoints` | `vector<pair<float, string>>` | All scale point values and display labels |
| `writableUri` | `string` | Non-empty for `AtomFilePath` controls; the property URI to pass to `setFilePath()` |

`getControlPortInfo()` iterates all control input ports in the live `ports_` vector. For each port it:
- creates three fresh Lilv nodes (`lv2:toggled`, `lv2:trigger`, `lv2:enumeration`) to classify the port
- reads min/max/default via `lilv_port_get_range()`
- calls `lilv_port_get_scale_points()` and stores every result; sets `isEnum = true` when scale points are present
- frees all Lilv nodes before returning

### `src/LiveEffectEngine.h` / `src/LiveEffectEngine.cpp` — `getPluginPortInfo()`

New method:
```cpp
std::vector<LV2Plugin::PortInfo> getPluginPortInfo(int slot);
```

Acquires `pluginMutex`, calls `plugin->getControlPortInfo()` for control-input ports, then appends one `AtomFilePath` entry per entry in `pluginInfo[uri]["writableParams"]` using the label and URI stored at scan time. Returns an empty vector if the slot is empty.

### `src/win32/ParameterPanel.h` / `src/win32/ParameterPanel.cpp` — new class

`ParameterPanel` owns a scrollable `"OpiqoParamPanel"` child window and all dynamic controls within it.

**Control mapping:**

| `ControlType` | `isEnum` | Win32 widget |
|---|---|---|
| `Float` | false | `TRACKBAR` (0–10000 integer range, linearly mapped to `[min, max]`) |
| `Float` | true | `COMBOBOX` (`CBS_DROPDOWNLIST`) populated with scale-point labels |
| `Toggle` | — | `BUTTON` (`BS_AUTOCHECKBOX`) |
| `Trigger` | — | `BUTTON` (`BS_PUSHBUTTON`, labelled "Trigger") |
| `AtomFilePath` | — | `BUTTON` (`BS_PUSHBUTTON`, labelled "Browse…") |

Each built control is recorded in a `ControlEntry` struct that stores `portIndex`, `type`, `minVal`, `maxVal`, `symbol`, `writableUri`, `enumValues[]`, and the control `HWND` and ID. This is used to map `WM_HSCROLL` / `WM_COMMAND` back to the correct engine call without any linear search by HWND.

**Scrolling:** `WS_VSCROLL`. `WM_VSCROLL` and `WM_MOUSEWHEEL` are both handled; `ScrollWindowEx` with `SW_SCROLLCHILDREN` repositions all child controls. `updateScrollInfo()` recomputes the scroll range whenever the panel is resized or rebuilt.

**Value path (all on UI thread, never from audio thread):**
- Trackbar `WM_HSCROLL` → `engine->setValue(slot, portIndex, mappedFloat)`
- Combobox `CBN_SELCHANGE` → `engine->setValue(slot, portIndex, enumValues[sel])`
- Checkbox `BN_CLICKED` → `engine->setValue(slot, portIndex, 0.0f or 1.0f)`
- Trigger `BN_CLICKED` → `engine->setValue(slot, portIndex, 1.0f)`
- Browse `BN_CLICKED` → `GetOpenFileNameA()` (blocking, UI thread only) → `engine->setFilePath(slot, writableUri, path)`

Public API:
- `registerClass(HINSTANCE)` — registers `"OpiqoParamPanel"`; call once at startup
- `build(parent, engine, slot, ports, baseId)` — destroys any existing controls, then creates label + widget per port entry; applies immediate layout
- `clear()` — `DestroyWindow` on every label and control, resets scroll state, hides the panel
- `resize(bounds)` — `MoveWindow` + `updateScrollInfo()`

### `src/win32/PluginSlot.h` / `src/win32/PluginSlot.cpp`

- Added `ParameterPanel paramPanel_` member.
- Added `buildParameterPanel(LiveEffectEngine*)` — calls `engine->getPluginPortInfo(slotIndex_ + 1)`, computes `baseId = IDC_PARAM_BASE + slotIndex_ * 500`, calls `paramPanel_.build()`, then immediately resizes the panel to the area below `y=60` (below the button row).
- Added `clearParameterPanel()` — delegates to `paramPanel_.clear()`.
- Updated `resize()` to compute a `panelBounds = {0, 60, w, h}` and call `paramPanel_.resize()` on every layout recalculation.

### `src/win32/resource.h`

Added:
```c
// Per-slot dynamic parameter controls:
//   slot s, port index p  ->  IDC_PARAM_BASE + s * 500 + p
#define IDC_PARAM_BASE 52000
```

500 IDs per slot (4 slots) = 2000 IDs reserved from 52000–53999, well clear of all existing ranges.

### `src/win32/MainWindow.cpp`

- Added `InitCommonControlsEx` call at the top of `create()` with `ICC_WIN95_CLASSES | ICC_BAR_CLASSES` to ensure `TRACKBAR_CLASS` is available.
- Added `ParameterPanel::registerClass(instance_)` alongside the existing `PluginSlot::registerClass()` call.
- Added `slots_[i].buildParameterPanel(&liveEngine_)` immediately after `slots_[i].setPlugin()` in the Add button handler.
- Added `slots_[i].clearParameterPanel()` immediately before `slots_[i].clearPlugin()` in the Delete button handler.

### `CMakeLists.txt`

Added `src/win32/ParameterPanel.cpp` to `WIN32_UI_SOURCES`.

---

## Decisions made during implementation

**`WM_HSCROLL` handled inside `ParameterPanel`, not forwarded to `MainWindow`.**  The slot's `SlotWndProc` forwards `WM_COMMAND` upward for Add/Bypass/Delete buttons. Parameter value changes are self-contained within `ParameterPanel` — the panel calls `engine->setValue()` directly, keeping `MainWindow` free of per-port dispatch logic.

**Scale-point extraction done at `getControlPortInfo()` call time, not at `init_ports()`.** This avoids storing an extra `enum_class_` node in `LV2Plugin` and keeps `init_ports()` unchanged. The Lilv queries are fast and only happen on the UI thread when a plugin is loaded.

**Degenerate range guard in `getControlPortInfo()`.** If a plugin reports `maxVal <= minVal` (common buggy edge case), `maxVal` is set to `minVal + 1.0f` before the value is returned, preventing divide-by-zero in the trackbar position mapping.

**`getPluginPortInfo()` merges control ports and atom writable params into one flat list.** This lets `ParameterPanel::build()` iterate a single vector without knowing about two separate metadata sources.

---

## Risks addressed

| Risk | Resolution |
|---|---|
| `lilv_port_get_scale_points` never called | Called in `getControlPortInfo()`; results stored in `PortInfo::scalePoints` |
| Ports with `minVal == maxVal` | Guarded in `getControlPortInfo()`: extends range to `[min, min+1]` |
| Panel unscrollable with many ports | `WS_VSCROLL` + `WM_VSCROLL` + `WM_MOUSEWHEEL` implemented in `ParameterPanel` |
| `setFilePath()` blocking atom read | Path left as-is (it is a logging/debug read, not on the audio thread); no audio safety issue |
| Trackbar `WM_HSCROLL` fires on every drag tick | `setValue()` holds `pluginMutex` briefly with no allocation; audio thread contention is negligible |
| `enum_class_` not passed into `LV2Plugin` | `getControlPortInfo()` creates its own `lv2:enumeration` node per call via direct Lilv API |
