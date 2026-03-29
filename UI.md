# Opiqo Windows UI Plan

## Architecture Overview

The UI is a thin Win32 layer on top of `LiveEffectEngine`. All audio processing stays in the engine; the UI only calls its public API. Parameter changes from the UI thread are posted via atomic/lock-free mechanisms so the audio thread is never blocked.

```
┌─────────────────────────────────────┐
│           Win32 UI Thread           │
│  MainWindow  │  Dialogs  │  Menus   │
└──────────────┬──────────────────────┘
               │ PostMessage / atomic
┌──────────────▼──────────────────────┐
│         LiveEffectEngine            │
│   plugin1..4 │ gain │ bypass        │
└──────────────┬──────────────────────┘
               │ callback
┌──────────────▼──────────────────────┐
│         WASAPI Audio Thread         │
│   process(input, output, frames)    │
└─────────────────────────────────────┘
```

---

## Files to Create

| File | Purpose |
|---|---|
| `src/win32/MainWindow.h/.cpp` | Top-level window, message loop, layout |
| `src/win32/PluginSlot.h/.cpp` | One plugin slot widget (add/bypass/delete + parameters) |
| `src/win32/PluginDialog.h/.cpp` | Plugin picker dialog (lists available plugins) |
| `src/win32/ControlBar.h/.cpp` | Bottom bar: power, gain, record |
| `src/win32/SettingsDialog.h/.cpp` | Audio settings and preferences |
| `src/win32/AudioEngine.h/.cpp` | WASAPI wrapper that calls `LiveEffectEngine::process()` |
| `src/win32/WasapiDeviceEnum.h/.cpp` | Enumerate WASAPI input/output devices |
| `src/win32/resource.h` | Win32 resource IDs |
| `src/win32/app.rc` | Application resource script (menus, icons) |
| `src/main_win32.cpp` | `WinMain` entry point |

---

## Main Window Layout

```
┌──────────────────────────────────────────────────────────┐
│ Menu: File | Devices | Settings                          │
├─────────────────────────┬────────────────────────────────┤
│  Slot 1                 │  Slot 2                        │
│  [+] [bypass] [x]       │  [+] [bypass] [x]              │
│  ┌──────────────────┐   │  ┌──────────────────┐          │
│  │ param sliders    │   │  │ param sliders    │          │
│  └──────────────────┘   │  └──────────────────┘          │
├─────────────────────────┼────────────────────────────────┤
│  Slot 3                 │  Slot 4                        │
│  [+] [bypass] [x]       │  [+] [bypass] [x]              │
│  ┌──────────────────┐   │  ┌──────────────────┐          │
│  │ param sliders    │   │  │ param sliders    │          │
│  └──────────────────┘   │  └──────────────────┘          │
├──────────────────────────────────────────────────────────┤
│ Control Bar (300px): [Power] Gain:[====] [● Record] [Wav:▾]│
├──────────────────────────────────────────────────────────┤
│ Status bar: [Input: ▾]  [Output: ▾]  48000 Hz | 4096 buf │
└──────────────────────────────────────────────────────────┘
```

Minimum window size: 900 x 650 px. Slots are arranged in a 2x2 equal-size grid.

---

## Component Details

### MainWindow (`MainWindow.h/.cpp`)
- Registers `WNDCLASSEX` and creates the main `HWND`.
- Creates and positions the four `PluginSlot` children, the `ControlBar`, and the status bar (`CreateStatusWindow`).
- Owns a single `LiveEffectEngine` instance and an `AudioEngine` instance.
- Handles `WM_SIZE` to resize children proportionally.
- Menu bar:
  - **File**: Export Preset, Import Preset, Exit
  - **Devices**: Input Device submenu + Output Device submenu populated at startup from `WasapiDeviceEnum`
  - **Settings**: opens `SettingsDialog`
- Device selection behavior:
  - Choosing an input or output device triggers `AudioEngine::stop()` -> reconfigure endpoint IDs -> `AudioEngine::start(...)`.
  - If restart fails, show error in status bar and keep previous working device IDs.

### Audio Device Selection (Input/Output)
- Status bar hosts two comboboxes: `[Input: ▾]` and `[Output: ▾]`.
- Device list source: `WasapiDeviceEnum` returns endpoint ID + friendly name + default flag.
- On startup:
  - Select persisted endpoint IDs if available.
  - If missing, fall back to system defaults.
- On user change (input or output):
  - Pause/stop stream.
  - Reinitialize WASAPI clients with selected endpoint IDs.
  - Resume stream and update status text (`sampleRate`, `blockSize`, mode).
- On hot-plug/default-change notification:
  - Refresh combobox and Devices menu entries.
  - Preserve current selection when still present; otherwise switch to default and restart audio.
- Persist settings in app config:
  - `inputDeviceId`, `outputDeviceId`, `sampleRate`, `blockSize`, `shareMode`.

### PluginSlot (`PluginSlot.h/.cpp`)
Each slot is a child `HWND` that manages:
- **Header row**: `[+]` button, plugin name label, bypass checkbox, `[×]` delete button.
- **Parameter panel**: scrollable child panel; rebuilt whenever a plugin is loaded/unloaded.
  - Each control port → a `TRACKBAR_CLASS` (slider) with label and current value text.
  - Toggle ports → `BS_CHECKBOX` button.
  - Dropdown/enum ports → `WC_COMBOBOX`.
  - File path ports → read-only edit + `[...]` browse button that calls `setFilePath`.
- On slider `WM_HSCROLL`, calls `LiveEffectEngine::setValue(slot, portIndex, newValue)` via a posted message or directly (values are `float`; the engine's mutex protects the write).
- The `[+]` button opens `PluginDialog` with the slot index; on OK calls `engine->addPlugin(slot, uri)` then rebuilds the parameter panel.
- The `[×]` button calls `engine->deletePlugin(slot)` and clears the panel.
- The bypass checkbox calls `engine->setPluginEnabled(slot, checked)`.

### PluginDialog (`PluginDialog.h/.cpp`)
- Modal dialog (`DialogBox`).
- On open: calls `engine->getAvailablePlugins()`, parses the returned JSON, and populates a `WC_LISTVIEW` with columns: Name, Author, URI.
- Search/filter edit box filters the list view in real time (no engine call needed, just filter the cached JSON).
- **Add** button (or double-click) returns the selected URI to the caller.

### ControlBar (`ControlBar.h/.cpp`)
- Child panel docked to bottom, fixed 48 px height.
- **Power toggle** (`BS_AUTOCHECKBOX`): controls audio engine on/off.
  - **On**: call `AudioEngine::start(sampleRate, blockSize, selectedInputDeviceId, selectedOutputDeviceId, shareMode)` using the device IDs selected in `[Input: ▾]` and `[Output: ▾]`.
  - **Off**: call `AudioEngine::stop()` and keep selected device IDs for next start.
  - If start fails, revert toggle to Off and show error in status bar.
- **Gain slider** (`TRACKBAR_CLASS`, 0–200 mapped to 0.0–2.0): writes to `*engine->gain` via `std::atomic` or the same lock.
- **Record button** (`BS_PUSHBUTTON`): opens a save-file dialog (`GetSaveFileName`) to get a file descriptor / path, then calls `engine->startRecording(fd, fileType, quality)`. While recording, label changes to "■ Stop"; clicking again calls `engine->stopRecording()`.
- Record format dropdown next to the record button: `[Wav: ▾]` (`WC_COMBOBOX` with WAV/MP3/OGG options).
- Optional quality dropdown (shown for lossy formats): `WC_COMBOBOX` for bitrate/quality presets.

### SettingsDialog (`SettingsDialog.h/.cpp`)
- Modal dialog with tabs (`WC_TABCONTROL`):
  - **Audio**: sample rate (`WC_COMBOBOX`: 44100/48000/96000), block size (combo: 256/512/1024/2048/4096), WASAPI mode (shared/exclusive radio buttons), explicit input/output device pickers bound to endpoint IDs.
  - **Presets**: Export/Import buttons that serialize/restore `engine->getPresetList()` to/from a JSON file.
- Applying changes: stop `AudioEngine`, reconfigure `LiveEffectEngine::sampleRate`/`blockSize`, restart.

### AudioEngine (`AudioEngine.h/.cpp`)
- Wraps WASAPI `IAudioClient` for capture and render.
- Dedicated audio thread: loops on `WaitForSingleObject(eventHandle)`, reads from capture endpoint, calls `engine->process(input, output, frames)`, writes to render endpoint.
- Exposes `start(sampleRate, blockSize, inputDeviceId, outputDeviceId)` and `stop()`.
- In shared mode: negotiates the mix format (32-bit float preferred).
- In exclusive mode: requests the format directly; falls back to shared if rejected.
- On `AUDCLNT_E_DEVICE_INVALIDATED`: posts `WM_APP_DEVICE_LOST` to the main window, which shows an error in the status bar and reopens device selection.

### WasapiDeviceEnum (`WasapiDeviceEnum.h/.cpp`)
- Uses `IMMDeviceEnumerator` to list capture and render endpoints.
- Returns a `std::vector<DeviceInfo>` with `{id, friendlyName, isDefault}`.
- Registers `IMMNotificationClient` to detect device changes; posts `WM_APP_DEVICE_CHANGED` to main window so the device menus can be refreshed.

---

## Thread Safety Model

| Data | Owner | Access from UI thread |
|---|---|---|
| `plugin1..4` | `LiveEffectEngine::pluginMutex` | Acquire mutex before add/delete |
| Port `control` values | Written by UI, read by audio thread | Use `std::atomic<float>` per port, or post via `LockFreeQueue` |
| `bypass` | `std::atomic<bool>` | Direct write |
| `*gain` | `std::atomic<float>` | Direct write |

Parameter updates from sliders → write to a `std::atomic<float>` in the port struct (change `float control` to `std::atomic<float>` in `LV2Plugin`). The audio thread reads it each process cycle with `relaxed` ordering.

---

## Build Integration

Add to `CMakeLists.txt`:
```cmake
set(WIN32_UI_SOURCES
    src/win32/MainWindow.cpp
    src/win32/PluginSlot.cpp
    src/win32/PluginDialog.cpp
    src/win32/ControlBar.cpp
    src/win32/SettingsDialog.cpp
    src/win32/AudioEngine.cpp
    src/win32/WasapiDeviceEnum.cpp
    src/main_win32.cpp
    src/win32/app.rc
)

add_executable(opiqo WIN32 ${WIN32_UI_SOURCES} ${ENGINE_SOURCES})
target_link_libraries(opiqo PRIVATE lilv-0 lv2 sndfile ole32 uuid mmdevapi)
```

Cross-compile on Linux with `x86_64-w64-mingw32-cmake`.

---

## Implementation Order

1. **AudioEngine** + **WasapiDeviceEnum** — validate WASAPI capture/render with a pass-through loop (no plugins).
2. **MainWindow** skeleton — empty window with status bar and four placeholder slot regions.
3. **ControlBar** — power, gain, record; wire up to engine.
4. **PluginDialog** + **PluginSlot** header row — add/delete/bypass without parameters.
5. **PluginSlot** parameter panel — dynamic slider/checkbox/combobox generation.
6. **SettingsDialog** — audio settings and preset import/export.
7. Polish: resizing, HiDPI awareness (`SetProcessDpiAwarenessContext`), error handling, status bar messages.
