# Milestone 6 — Plugin Management UI

## Changed files

### `src/win32/resource.h`
Six new identifiers added:

| Identifier | Value | Purpose |
|---|---|---|
| `IDC_SLOT_ADD_BASE` | `51000` | Add Plugin button base ID; add slot index (0–3) for each slot |
| `IDC_SLOT_BYPASS_BASE` | `51010` | Bypass/Enable button base ID |
| `IDC_SLOT_DELETE_BASE` | `51020` | Delete button base ID |
| `IDC_PLUGIN_LIST` | `51100` | ListBox control inside `PluginDialog` |

### `src/win32/PluginSlot.h`
Complete rewrite — the class is now backed by a custom registered window class instead of a plain `STATIC`:

New static method:
- `registerClass(HINSTANCE)` — registers `"OpiqoPluginSlot"` with `SlotWndProc`. Must be called once before any slot is created. Returns `true` on success or if the class is already registered.

`create()` signature changed:
- Parameter `int id` renamed to `int slotIndex` (0–3); slotIndex is stored and used for label reset.

Removed methods:
- `setLabel(const char*)` — replaced by `setPlugin()` and `clearPlugin()`.

New public methods:

| Method | Description |
|---|---|
| `setPlugin(const char* name)` | Sets label to plugin name; enables Bypass and Delete buttons |
| `clearPlugin()` | Resets label to "Slot N"; disables Bypass and Delete; resets Bypass text to "Bypass" |
| `setBypassVisual(bool bypassed)` | Sets Bypass button text to "Enable" when bypassed, "Bypass" when active |

New private members:

| Member | Purpose |
|---|---|
| `labelStatic_` | `HWND` — `STATIC` text showing slot number or active plugin name |
| `addButton_` | `HWND` — "Add Plugin" pushbutton, always enabled |
| `bypassButton_` | `HWND` — "Bypass"/"Enable" pushbutton, disabled when slot is empty |
| `deleteButton_` | `HWND` — "Delete" pushbutton, disabled when slot is empty |
| `slotIndex_` | `int` — stored slot index used by `clearPlugin()` for label reset |
| `SlotWndProc` | Static `WNDPROC` — forwards `WM_COMMAND` to the parent window (MainWindow) |

### `src/win32/PluginSlot.cpp`
Complete rewrite:

#### `registerClass()`
Registers `"OpiqoPluginSlot"` with `COLOR_BTNFACE` background and `SlotWndProc`. Tolerates `ERROR_CLASS_ALREADY_EXISTS`.

#### `SlotWndProc()`
Minimal WndProc: on `WM_COMMAND` calls `SendMessage(GetParent(hwnd), WM_COMMAND, wParam, lParam)` to bubble the notification up to `MainWindow`. All other messages forwarded to `DefWindowProcA`.

#### `create(parent, slotIndex, bounds)`
Creates the container via `CreateWindowExA(WS_EX_CLIENTEDGE, "OpiqoPluginSlot", ...)` with `WS_CLIPCHILDREN`. Then creates four child controls:

| Control | Style | Initial state | Position |
|---|---|---|---|
| Label `STATIC` | `SS_LEFT` | "Slot N" | 4, 4, width−8, 20 |
| Add Plugin `BUTTON` | `BS_PUSHBUTTON` | Enabled | 4, 30, 90×24 |
| Bypass `BUTTON` | `BS_PUSHBUTTON` | **Disabled** | 100, 30, 70×24 |
| Delete `BUTTON` | `BS_PUSHBUTTON` | **Disabled** | 176, 30, 70×24 |

Button IDs are `IDC_SLOT_*_BASE + slotIndex`.

#### `resize(bounds)`
Calls `MoveWindow` on the container and then repositions all four child controls with fixed offsets within the new client area.

### `src/win32/PluginDialog.h`
- Added `LiveEffectEngine` forward declaration.
- `showModal` signature updated: `static bool showModal(HWND parent, LiveEffectEngine* engine, std::string* selectedUri)`.

### `src/win32/PluginDialog.cpp`
Full implementation replacing the `MessageBoxA` stub:

#### `PluginDialogState` struct
Internal state carried via `GWLP_USERDATA`:

| Field | Purpose |
|---|---|
| `uris` | Parallel vector of plugin URI strings |
| `names` | Parallel vector of display names |
| `selectedUri` | Output pointer from the caller |
| `confirmed` | Set to `true` when user confirmed a selection |
| `listBox` | `HWND` of the `LISTBOX` control |

#### `PluginDlgWndProc()`
Window procedure for the dialog window:

- `WM_CREATE`: stores state pointer; creates a sorted `LISTBOX` (`LBS_NOTIFY | LBS_SORT`) filling most of the window; adds all plugin display names; creates OK and Cancel buttons.
- `WM_COMMAND / IDOK` or `LBN_DBLCLK`: reads selected name from listbox via `LB_GETTEXT`, scans `state->names` for a match, writes the corresponding URI to `*selectedUri`, sets `confirmed = true`, calls `DestroyWindow`.
- `WM_COMMAND / IDCANCEL`: calls `DestroyWindow`.
- `WM_DESTROY`: calls `PostQuitMessage(0)` to exit the local message loop.

#### `showModal()`
1. Calls `engine->getAvailablePlugins()` and builds parallel `uris` / `names` arrays.
2. Shows `"No LV2 plugins found."` and returns `false` if the list is empty.
3. Registers `"OpiqoPluginDlg"` window class on first call (guarded by a local static).
4. Disables the parent window.
5. Creates a `480×360` `WS_POPUP | WS_CAPTION | WS_SYSMENU` window, centres it over the parent, shows it.
6. Runs a local `GetMessage` loop until `PostQuitMessage` fires from `WM_DESTROY`.
7. Re-enables the parent, calls `SetForegroundWindow`.
8. Returns `state.confirmed`.

### `src/win32/MainWindow.h`
- Added `#include "PluginDialog.h"`.
- Added `slotEnabled_[4]` (`bool`, default `true`) — per-slot enabled state used to toggle bypass without querying the engine.

### `src/win32/MainWindow.cpp`

#### `create()` additions
- Calls `PluginSlot::registerClass(instance_)` immediately after the main window class is registered.
- Slot creation loop simplified: `slots_[i].create(hwnd_, i, dummy)` — labels are now set internally by `PluginSlot`.

#### `WM_COMMAND` default branch (new range handlers)
Three mutually exclusive ID-range cases added after the named command cases:

**Add Plugin** (`IDC_SLOT_ADD_BASE + i`):
1. Opens `PluginDialog::showModal(hwnd_, &liveEngine_, &uri)`.
2. On confirmation: `liveEngine_.addPlugin(i + 1, uri)`.
3. Looks up `"name"` in `getAvailablePlugins()[uri]`; falls back to URI string.
4. Calls `slots_[i].setPlugin(name)` and sets `slotEnabled_[i] = true`.

**Bypass/Enable** (`IDC_SLOT_BYPASS_BASE + i`):
1. Toggles `slotEnabled_[i]`.
2. Calls `liveEngine_.setPluginEnabled(i + 1, slotEnabled_[i])`.
3. Calls `slots_[i].setBypassVisual(!slotEnabled_[i])`.

**Delete** (`IDC_SLOT_DELETE_BASE + i`):
1. Calls `liveEngine_.deletePlugin(i + 1)`.
2. Calls `slots_[i].clearPlugin()`.
3. Resets `slotEnabled_[i] = true`.
