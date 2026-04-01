# Milestone 9 — Settings Dialog and Runtime Reconfiguration

## What the milestone requires

- Settings dialog with audio settings tab: sample rate, block size, share mode, explicit input/output endpoint pickers.
- Apply flow: if running, stop engine → apply settings → restart with new values.
- Preset export/import using `getPresetList()` and a restore path.

---

## What already exists (usable as-is)

| Component | Status |
|---|---|
| `AppSettings` | All fields present: `inputDeviceId`, `outputDeviceId`, `sampleRate`, `blockSize`, `exclusiveMode`, `recordFormat`, `recordQuality`. JSON load/save to `%APPDATA%\Opiqo\settings.json` fully implemented. |
| `AudioEngine` | `start(sampleRate, blockSize, inputDeviceId, outputDeviceId, exclusiveMode)` and `stop()` (synchronous) fully implemented. `state()` and `errorMessage()` available. |
| `WasapiDeviceEnum` | `enumerateInputDevices()`, `enumerateOutputDevices()`, `resolveOrDefault()` all implemented. |
| `LiveEffectEngine::getPresetList()` | Returns a JSON string with `app`, `gain`, and per-slot preset objects (`plugin1`–`plugin4`). Each plugin entry maps port symbol → current float value. |
| `LiveEffectEngine::getPreset(int)` | Per-slot preset as a `json` object (symbol → value). |
| `LiveEffectEngine::setValue(int slot, int portIndex, float value)` | Applies a value to a port by index under `pluginMutex`. |
| `LiveEffectEngine::getPluginPortInfo(int slot)` | Returns `vector<LV2Plugin::PortInfo>` with `symbol` and `control` fields, usable to map symbol names back to port indices during import. |
| Menu items | `IDM_SETTINGS_OPEN`, `IDM_FILE_EXPORT_PRESET`, `IDM_FILE_IMPORT_PRESET` exist in `resource.h` and `app.rc`. All three are wired in `MainWindow::handleMessage()`—but all three currently show a "Milestone 0 scaffolded" `MessageBoxA` placeholder. |
| `SettingsDialog` files | `SettingsDialog.h` (header-only stub, `static void show(HWND)`) and `SettingsDialog.cpp` (1-line MessageBox stub) exist. The class shell is in place. |

---

## What was implemented

### `src/win32/resource.h`
- Added `IDD_SETTINGS 200`.
- Added `IDC_SETTINGS_SAMPLERATE 60001`, `IDC_SETTINGS_BLOCKSIZE 60002`, `IDC_SETTINGS_EXCLUSIVE 60003`, `IDC_SETTINGS_INPUT 60004`, `IDC_SETTINGS_OUTPUT 60005`.

### `src/win32/app.rc`
- Added `IDD_SETTINGS DIALOG` template (300×200 DLUs) with labels, combos, exclusive-mode checkbox, and OK/Cancel buttons. Uses `DS_SETFONT | DS_MODALFRAME` style.

### `src/win32/SettingsDialog.h`
- Signature changed to `static bool show(HWND parent, AppSettings* settings, WasapiDeviceEnum* deviceEnum)` — returns `true` on OK.
- Added includes for `AppSettings.h` and `WasapiDeviceEnum.h`.
- `AudioEngine*` parameter dropped from the signature; the caller (`MainWindow`) owns the stop/restart decision.

### `src/win32/SettingsDialog.cpp`
- Full `DialogBoxParamA` + `SettingsDialogProc` implementation.
- `WM_INITDIALOG`: populates sample-rate combo (44100/48000/96000), block-size combo (256/512/1024/2048/4096), exclusive-mode checkbox, input device combo, output device combo — all pre-selected to match `*settings`.
- `IDOK`: reads all combos/checkbox back, writes into `*settings`, calls `EndDialog(IDOK)`. `IDCANCEL` closes without writing.
- Device friendly names sourced from `WasapiDeviceEnum::enumerateInputDevices()` / `enumerateOutputDevices()`; device IDs stored in `SettingsContext` vectors so the index maps back to the correct ID on OK.

### `src/win32/ControlBar.h` / `ControlBar.cpp`
- Added `enableRecordButton(bool)` — calls `EnableWindow(recordButton_, ...)`.

### `src/LiveEffectEngine.h` / `LiveEffectEngine.cpp`
- Added `void applyPreset(int slot, const json& preset)`.  
  Builds a `symbol → portIndex` map from `getPluginPortInfo(slot)`, iterates the preset JSON object, calls `setValue(slot, portIndex, value)` for each matching symbol. Unrecognised symbols silently skipped.
- Added `#include <unordered_map>` to `LiveEffectEngine.cpp`.
- `gain` is still a public `float*`; import writes directly with a null guard — no separate setter added.

### `src/win32/MainWindow.cpp`
- Added `#include <fstream>` and `#include "SettingsDialog.h"`.
- **`IDM_FILE_EXPORT_PRESET`**: `GetSaveFileNameA` (`.json` filter) → `ofstream` write of `liveEngine_.getPresetList()`. Error shown on file-open failure.
- **`IDM_FILE_IMPORT_PRESET`**: `GetOpenFileNameA` → `ifstream` → JSON parse → `applyPreset()` for slots 1–4 → gain restore with null guard. Corrupt/unreadable files caught in `catch(...)` and shown as a warning `MessageBox`.
- **`IDM_SETTINGS_OPEN`**: guards against active recording (`recordingFd_ >= 0`); calls `SettingsDialog::show()`; on OK — stops engine if running, applies + saves settings, calls `onDeviceListChanged()` to refresh status bar, restarts engine if it was running (reusing `IDT_ENGINE_STATE` poll).
- Power-toggle Off path: added `controlBar_.enableRecordButton(false)`.
- `onEngineStatePoll()` Running path: added `controlBar_.enableRecordButton(true)`.
- `onEngineStatePoll()` Error path: added `controlBar_.enableRecordButton(false)`.
- Startup (`create()`): `controlBar_.enableRecordButton(false)` called immediately after `controlBar_.create()` — record is locked until the engine reaches Running.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| `SettingsDialog` has no `AudioEngine*` parameter | The caller owns the stop/restart logic; keeping the dialog pure avoids coupling it to the engine state machine |
| `restoreGain` not added as a named method | `gain` is already a public `float*`; direct null-guarded write in the import handler is sufficient without adding API surface |
| Settings-apply restart reuses `IDT_ENGINE_STATE` timer | No new timer or thread needed; `onEngineStatePoll()` already handles both Running and Error outcomes |
| Recording blocks settings apply | If `recordingFd_ >= 0`, `IDM_SETTINGS_OPEN` shows an informational message and returns — prevents block-size change while encoder is writing |
| Preset import is best-effort | Unrecognised symbols (different plugin version, empty slot) are silently skipped; corrupt JSON files surface a warning dialog |
