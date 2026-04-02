# Milestone 12 — Preset Round-Trip Completeness and ControlBar Layout

## What the milestone requires

- Preset export must include the URI of every loaded plugin so the preset is self-describing.
- Preset import must load the correct plugin into each slot (calling `addPlugin` when the URI is present) before applying parameter values.
- Parameter panel controls must visually reflect the values just restored by an import—without requiring the user to reload the panel manually.
- `ControlBar::resize()` must reposition its child controls so the layout stays coherent at all window widths.
- The status bar must be reliably visible at all times with no overlap or race with the control bar.

Acceptance criteria:
- Export a session with four plugins loaded. Delete all plugins. Import the preset. All four plugins are reloaded with their parameter panels rebuilt and values restored.
- Export a preset. Import it to a machine whose slots contain different plugins. The correct plugins are loaded and configured.
- Resize the window to near-minimum width. All control bar widgets remain visible and within the bar bounds.
- On every window size and state (initial, maximised, restored, resized), the status bar is visible at the bottom and the power button is fully clickable.

---

## What already exists (usable as-is)

| Component | Status |
|---|---|
| `lilv_plugin_get_uri(p->plugin_)` | **Done.** Already used in `getPluginPortInfo()` and `initPlugins()`. Same call pattern reads the URI for any loaded slot. |
| `addPlugin(slot, uri)` | **Done.** Takes a slot index (1–4) and a URI string. Handles bypass, mutex, teardown of existing plugin, and instantiation of the new one. Returns -1 on failure. |
| Plugin name lookup pattern | **Done.** `getAvailablePlugins()` returns `{uri: {name: "..."}}`. The Add-plugin handler in `MainWindow` already resolves a display name from this map using `all[uri]["name"]`. Identical pattern applies after preset import. |
| `buildParameterPanel(&liveEngine_)` | **Done.** Called in `PluginSlot`. Calls `getPluginPortInfo(slot)` to get current engine port state and rebuilds all Win32 controls from scratch. |
| `clearParameterPanel()` | **Done.** Destroys existing controls and hides the panel. |
| `ControlBar` child control positions | **Done** (positions known): `powerButton_` x=4 w=80, `gainSlider_` x=92 w=120, `recordButton_` x=220 w=80, `formatCombo_` x=308 w=100, `qualityCombo_` x=416 w=90. All at y=4, height 28 (buttons) or 20 (slider). |

---

## What was implemented

### `src/LiveEffectEngine.cpp` — `getPreset()`

Added a `"uri"` field immediately before the port-value loop:

```cpp
json preset = {};
const LilvNode* uriNode = lilv_plugin_get_uri(p->plugin_);
if (uriNode)
    preset["uri"] = lilv_node_as_string(uriNode);
for (const auto& port : p->ports_)
    preset[port.symbol] = port.control;
return preset;
```

`getPresetList()` required no change — it delegates to `getPreset()` for each slot and assembles the top-level JSON. A preset exported after this change has the form:

```json
{
  "app": "opiqo-desktop",
  "gain": 0.8,
  "plugin1": { "uri": "http://guitarix.sourceforge.net/plugins/gx_compressor#_compressor", "attack": 0.01 },
  "plugin2": { "uri": "...", ... }
}
```

### `src/win32/MainWindow.cpp` — `IDM_FILE_IMPORT_PRESET` handler

Replaced the minimal four-line per-slot apply loop with a full plugin-load-and-rebuild block. `getAvailablePlugins()` is called once before the loop to avoid redundant full-scan copies. Per slot:

1. Skips the slot if the subobject is absent or not an object.
2. Checks for `"uri"`. If present, calls `liveEngine_.addPlugin(s, uri)`. On failure, shows a `MessageBoxA` warning and `continue`s to the next slot.
3. On success, resolves the display name from the `getAvailablePlugins()` map, calls `slots_[s-1].setPlugin(name)`, and sets `slotEnabled_[s-1] = true`.
4. Calls `liveEngine_.applyPreset(s, slotPreset)` to restore parameter values.
5. Calls `slots_[s-1].clearParameterPanel()` then `slots_[s-1].buildParameterPanel(&liveEngine_)` to rebuild controls from the now-current engine state.

The existing gain-restore block (which writes `*liveEngine_.gain`, `settings_.gain`, and updates the slider) was left unchanged before the new loop.

### `src/win32/ControlBar.cpp` — `resize()`

Extended `resize()` to reposition all five child controls after moving the container:

```cpp
void ControlBar::resize(const RECT& bounds) {
    if (!hwnd_) return;
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    MoveWindow(hwnd_, bounds.left, bounds.top, w, h, TRUE);

    if (powerButton_)  MoveWindow(powerButton_,  4,   4,  80,  28, TRUE);
    if (gainSlider_)   MoveWindow(gainSlider_,   92,  8,  120, 20, TRUE);
    if (recordButton_) MoveWindow(recordButton_, 220, 4,  80,  28, TRUE);
    if (formatCombo_)  MoveWindow(formatCombo_,  308, 4,  100, 120, TRUE);
    if (qualityCombo_) MoveWindow(qualityCombo_, 416, 4,  90,  120, TRUE);
}
```

Positions are taken verbatim from `create()`, keeping initial and resize layouts in sync. No header change was required.

### `src/win32/MainWindow.cpp` — `create()` comment

Updated the status-bar creation comment from "along the top" to "along the bottom":

```cpp
// Status bar along the bottom of the client area.
// STATUSCLASSNAME docks itself to the bottom automatically when it
// receives WM_SIZE; doLayout() triggers this by sending WM_SIZE to it.
```

### `src/win32/MainWindow.cpp` — `doLayout()`

Replaced the `SetWindowPos`-based status-bar positioning block with a `SendMessage`/`GetWindowRect` approach, and adjusted the slot grid and control bar coordinates to place the status bar at the bottom:

```cpp
int sbH = kStatusHeight;
if (statusBar_) {
    SendMessage(statusBar_, WM_SIZE, 0, 0);
    int parts[2] = { totalW / 2, -1 };
    SendMessage(statusBar_, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
    RECT sbrc = {};
    GetWindowRect(statusBar_, &sbrc);
    int actualH = sbrc.bottom - sbrc.top;
    if (actualH > 0) sbH = actualH;
}

int slotAreaTop = 0;
int slotAreaBot = totalH - kBarHeight - sbH;
// ...
RECT barBounds = { 0, slotAreaBot, totalW, slotAreaBot + kBarHeight };
controlBar_.resize(barBounds);
```

`kStatusHeight` (22) is retained as the fallback if `GetWindowRect` returns zero on the first call before the window is fully visible.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| `getAvailablePlugins()` outside loop | Returns the full cached plugin scan as a JSON object. Called once before the import loop to avoid four redundant deep copies; the reference is read-only inside the loop. |
| `clearParameterPanel()` before `buildParameterPanel()` | A safety net for the case where the slot already had the same plugin and `addPlugin()` was not called. `buildParameterPanel()` calls `ParameterPanel::build()` which calls `clear()` internally, but only if there are ports to build. The explicit `clear` ensures no stale controls remain if the engine returns an empty port list. |
| `continue` on `addPlugin` failure | If a plugin cannot be loaded, `applyPreset` and panel rebuild are skipped for that slot. A `MessageBoxA` warning identifies the failing URI. Other slots in the preset continue to be processed. |
| `STATUSCLASSNAME` bottom-dock | `STATUSCLASSNAME` has a built-in WndProc that docks itself to the bottom of its parent when it receives `WM_SIZE`. The previous `SetWindowPos` to y=0 raced against this auto-dock, producing non-deterministic layout. The fix embraces the built-in behaviour: `doLayout()` sends `WM_SIZE` explicitly to the status bar (since `WM_SIZE` on the parent goes to our handler, not `DefWindowProc`), then reads back the actual height with `GetWindowRect` for DPI/theme awareness. |
| Control bar bounds use `slotAreaBot + kBarHeight` | Previously `{ 0, slotAreaBot, totalW, totalH }` was used, which overextended the bar into the status bar region and could obstruct it. The explicit `slotAreaBot + kBarHeight` right-bounds the control bar precisely. |



---

## File change summary

| File | Change type | Description |
|---|---|---|
| `src/LiveEffectEngine.cpp` | Edit `getPreset()` | Add `"uri"` field from `lilv_plugin_get_uri` |
| `src/win32/MainWindow.cpp` | Edit import handler | Load plugin from `"uri"`, rebuild panel, apply parameters |
| `src/win32/ControlBar.cpp` | Edit `resize()` | Reposition all five child controls after resizing container |
| `src/win32/MainWindow.cpp` | Edit `doLayout()` | Status bar: SendMessage(WM_SIZE) + GetWindowRect; slot area top=0; control bar above status bar |
| `src/win32/MainWindow.cpp` | Edit comment | Status bar creation comment updated to reflect bottom-dock behaviour |

No new files. No CMakeLists change required.

---

## Testing plan

### Preset round-trip — full session restore
1. Load a different plugin into each of the four slots. Adjust any parameter.
2. Export the preset (File > Export Preset). Open the JSON; confirm every `plugin1`–`plugin4` subobject has a `"uri"` key.
3. Delete all four plugins (Delete button on each slot).
4. Import the preset (File > Import Preset).
5. Verify: all four plugins are reloaded, slot headers show the correct plugin names, parameter panels are populated, and control values (sliders, checkboxes) match the exported state.

### Preset round-trip — cross-session compatible
1. Import the preset from step 2 above starting from a blank session (no plugins loaded).
2. Verify the same outcome as step 5.

### Preset round-trip — partial: slots with no URI
1. Export a preset, then manually remove the `"uri"` key from `plugin2` using a text editor.
2. Import the modified preset.
3. Verify: slots 1, 3, 4 restore correctly; slot 2 shows a warning dialog with the missing-plugin message and remains in its current state.

### Preset round-trip — existing different plugin
1. Load plugin A into slot 1. Export preset.
2. Delete plugin A; load plugin B into slot 1.
3. Import the original preset.
4. Verify: slot 1 now contains plugin A (not plugin B), with correct values.

### ControlBar resize
1. Launch the app. Resize the window horizontally to approximately 600 px wide.
2. Verify: Power, Gain slider, Record, WAV dropdown, and Quality dropdown all remain visible inside the control bar and are not clipped.
3. Restore to full size. Verify no visual artefacts.

### Status bar layout
1. Launch the app. Verify: status bar is visible at the bottom edge of the window showing "In: (none)" and "Out: (none)".
2. Maximise the window. Verify: status bar remains at the bottom; slot grid fills the area above the control bar; no gap or overlap between status bar and control bar.
3. Restore and manually resize. Verify: status bar snaps to the bottom on every resize.
4. Click the Power button. Verify it is fully clickable (no invisible HWND blocking it).
5. Start the engine; confirm "In: <device>" and "Out: <device>" appear in the correct halves of the status bar.
