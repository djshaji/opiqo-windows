# Milestone 11 — Gain Control Wiring, Settings Persistence, and Safe Shutdown

## What the milestone requires

- The gain slider in the control bar must visibly and audibly control `liveEngine_.gain` in real time.
- Gain value must persist across sessions (saved in `settings.json`, restored at startup).
- Gain slider position must stay in sync with the engine value after preset import.
- Closing the window while a recording is in progress must prompt the user and safely flush and close the output file.
- The application must have a Help > About menu item displaying version information.

Acceptance criteria:
- Dragging the gain slider changes audio output level immediately; the app can be restarted and the slider is at the same position.
- Importing a preset whose `"gain"` field differs from the current slider position causes the slider to snap to the correct position.
- Closing during recording shows a confirmation dialog; choosing Cancel leaves recording running; choosing OK finalises the file and closes the app.
- Help > About shows the app name and version in a message box.

---

## What already existed (usable as-is)

| Component | Status |
|---|---|
| `ControlBar::gainValue()` | **Done.** Returns `TBM_GETPOS` for `gainSlider_`. |
| `ControlBar::gainSlider_` | **Done.** Created in `ControlBar::create()`. Range 0–100, default position 80. |
| `liveEngine_.gain` | **Done.** `float*` malloc'd to `1.0f` in `LiveEffectEngine` constructor. Checked for null before writes in import path. |
| Preset import — engine write | **Done.** `*liveEngine_.gain = preset["gain"].get<float>()` already in `IDM_FILE_IMPORT_PRESET` handler. |
| `WM_DESTROY` — `settings_.save()` | **Done.** `settings_.save()` is already called in `WM_DESTROY`. |
| `AppSettings` load/save | **Done.** JSON serialisation via `nlohmann/json`; `%APPDATA%\Opiqo\settings.json`. |
| Recording stop on device loss | **Done.** `onEngineError()` already calls `liveEngine_.stopRecording()` and `_close(recordingFd_)`. |

---

## What already existed (usable as-is)

| Component | Status |
|---|---|
| `ControlBar::gainValue()` | **Done.** Returns `TBM_GETPOS` for `gainSlider_`. |
| `ControlBar::gainSlider_` | **Done.** Created in `ControlBar::create()`. Range 0–100, default position 80. |
| `liveEngine_.gain` | **Done.** `float*` malloc'd to `1.0f` in `LiveEffectEngine` constructor. Checked for null before writes in import path. |
| Preset import — engine write | **Done.** `*liveEngine_.gain = preset["gain"].get<float>()` already in `IDM_FILE_IMPORT_PRESET` handler. |
| `WM_DESTROY` — `settings_.save()` | **Done.** `settings_.save()` is already the first statement in `WM_DESTROY`. |
| `AppSettings` load/save | **Done.** JSON serialisation via `nlohmann/json`; `%APPDATA%\Opiqo\settings.json`. Adding a new field only requires adding one `j["gain"] = gain` line in `save()` and one `gain = j.value("gain", 0.8f)` in `load()`. |
| Recording stop on device loss | **Done.** `onEngineError()` already calls `liveEngine_.stopRecording()` and `_close(recordingFd_)`. |

---

## What was implemented

### `src/win32/ControlBar.h`

- Added `void setGainValue(int pos)` public method declaration.

### `src/win32/ControlBar.cpp`

- Implemented `setGainValue(int pos)` after `gainValue()`. Clamps `pos` to `[0, 100]` then calls `SendMessage(gainSlider_, TBM_SETPOS, TRUE, pos)`. No-op if `gainSlider_` is null.

### `src/win32/AppSettings.h`

- Added `float gain = 0.8f` field to `AppSettings`. Default `0.8f` matches the slider's hard-coded initial position of 80 in `ControlBar::create()`.

### `src/win32/AppSettings.cpp`

- **`load()`**: added `s.gain = j.value("gain", 0.8f)` to deserialise the new field. Falls back to `0.8f` if the key is absent (first launch or settings from an older version).
- **`save()`**: added `j["gain"] = gain` to serialise the field.

### `src/win32/MainWindow.h`

- Added private method `void onGainChanged()`.
- Added static private method `LRESULT CALLBACK ControlBarSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR)`.

### `src/win32/MainWindow.cpp`

**`create()` — subclass installation and startup sync**

After `controlBar_.create(hwnd_, dummy)`, installs a window subclass on the ControlBar container (`STATIC` class) using `SetWindowSubclass`:

```cpp
SetWindowSubclass(controlBar_.hwnd(), ControlBarSubclassProc,
                  1 /*subclassId*/, reinterpret_cast<DWORD_PTR>(this));
```

Immediately after, syncs the slider and engine to the persisted gain value:

```cpp
if (liveEngine_.gain)
    *liveEngine_.gain = settings_.gain;
controlBar_.setGainValue(static_cast<int>(settings_.gain * 100.0f + 0.5f));
```

The `+ 0.5f` ensures correct rounding rather than truncation.

**`ControlBarSubclassProc()` (new, above `WndProc`)**

Intercepts `WM_HSCROLL` sent by the trackbar to its parent (the `STATIC` container). The `STATIC` class WndProc discards `WM_HSCROLL` via `DefWindowProc`; this subclass intercepts it first. On `WM_HSCROLL`, casts `refData` back to `MainWindow*` and calls `self->onGainChanged()`. All other messages are forwarded to `DefSubclassProc` to preserve the original behaviour.

**`onGainChanged()` (new)**

Called on every slider movement:
1. Guards on `liveEngine_.gain != nullptr`.
2. Reads `controlBar_.gainValue()` — the current slider position in `[0, 100]`.
3. Converts to float: `g = pos / 100.0f`.
4. Writes `*liveEngine_.gain = g` (takes effect in the very next audio callback).
5. Writes `settings_.gain = g` (in-memory; flushed to disk by the existing `settings_.save()` call in `WM_DESTROY` on the next clean shutdown).

**`IDM_FILE_IMPORT_PRESET` handler — gain slider sync**

Extended the existing `if (preset.contains("gain") ...)` block to also update the slider and settings after writing the engine value:

```cpp
*liveEngine_.gain = preset["gain"].get<float>();
float g = *liveEngine_.gain;
settings_.gain = g;
controlBar_.setGainValue(static_cast<int>(g * 100.0f + 0.5f));
```

**`WM_CLOSE` handler (new)**

Added before `WM_DESTROY`. If `recordingFd_ >= 0` (a recording is active), shows a `MB_YESNO` confirmation dialog. If the user selects No, returns `0` to cancel the close. If Yes (or no recording is active), calls `liveEngine_.stopRecording()`, `_close(recordingFd_)`, resets `recordingFd_ = -1`, clears the record button state, then calls `DestroyWindow(hwnd_)`.

**`WM_DESTROY` handler — safety-net recording flush**

Added a safety-net block at the top of `WM_DESTROY`, before `settings_.save()`:

```cpp
if (recordingFd_ >= 0) {
    liveEngine_.stopRecording();
    _close(recordingFd_);
    recordingFd_ = -1;
}
```

This handles any path that bypasses `WM_CLOSE` (e.g. a direct `DestroyWindow` call from `onEngineError` or elsewhere). `settings_.save()` follows the safety-net so `settings_.gain` is always written to disk regardless of how shutdown was triggered.

**`IDM_HELP_ABOUT` handler (new)**

Added in the `WM_COMMAND` switch alongside `IDM_FILE_EXIT`:

```cpp
case IDM_HELP_ABOUT:
    MessageBoxA(hwnd_,
        "Opiqo Windows Host\nVersion 1.0"
        "\n\nLV2 audio plugin host with WASAPI duplex audio.",
        "About Opiqo", MB_OK | MB_ICONINFORMATION);
    return 0;
```

### `src/win32/resource.h`

- Added `#define IDM_HELP_ABOUT 40005` (sequential with `IDM_SETTINGS_OPEN 40004`).

### `src/win32/app.rc`

- Added `POPUP "&Help"` with `MENUITEM "&About Opiqo...", IDM_HELP_ABOUT` after the `Settings` popup in `IDR_MAINMENU`.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| `WM_HSCROLL` routing | The ControlBar container is a `STATIC`-class child window. `STATIC` discards `WM_HSCROLL` in its default proc. `SetWindowSubclass` was used rather than re-parenting the slider or replacing the STATIC with a custom class — it requires no structural change and is the recommended Win32 approach for this exact scenario. |
| Gain persistence granularity | `settings_.gain` is written in-memory on every slider move; `settings_.save()` is not called on each drag to avoid repeated file I/O in the hot path. The existing `WM_DESTROY` call to `settings_.save()` ensures persistence on clean exit. |
| Startup rounding | `static_cast<int>(settings_.gain * 100.0f + 0.5f)` is used when converting the stored float back to a slider position. Without `+ 0.5f`, values like `0.8f * 100 = 79.999...` truncate to 79 instead of 80. |
| Gain default | `0.8f` was chosen to match the trackbar's pre-existing hard-coded default position of 80. This keeps the slider visual consistent between a fresh install (no `settings.json`) and subsequent launches. |
| `WM_CLOSE` vs `WM_DESTROY` recording guard | `WM_CLOSE` is the correct interception point for user-initiated close. The `WM_DESTROY` safety net covers programmatic `DestroyWindow` calls (e.g. from device-loss recovery). Both code paths call `liveEngine_.stopRecording()` before `_close()` to ensure codec finalisation. |
| About dialog | A `MessageBoxA` with `MB_ICONINFORMATION` was used rather than a separate `DIALOG` resource — sufficient for the scope of this milestone and requires no `.rc` dialog block or separate dialog proc. |

---

## Testing plan

### Gain slider — basic wiring
1. Launch app. Confirm slider is at 80% visually.
2. Load an audio source. Start the engine.
3. Drag slider to 0: confirm audio is silenced.
4. Drag slider to 100: confirm audio at full level.
5. Drag to 50. Close the app and relaunch. Confirm slider reopens at 50%.

### Gain slider — preset sync
1. Export a preset while slider is at 50. Manually edit the JSON to set `"gain": 0.9`.
2. Import the edited preset. Confirm slider snaps to 90%.
3. Verify `*liveEngine_.gain ≈ 0.9f` takes effect (audible level change).

### Safe close — recording active
1. Start engine. Start a WAV recording.
2. Attempt to close the window (Alt+F4 or × button).
3. Confirm a dialog appears. Click Cancel. Verify app stays open and recording continues.
4. Click × again. Click OK. Verify the app closes and the WAV file is valid (non-zero size, playable).

### Safe close — recording active, OGG/MP3
1. Repeat above with OGG and MP3 formats. Verify codec flush produces a valid file.

### About dialog
1. Click Help > About Opiqo. Verify a message box appears with the correct text. Click OK; verify it closes normally.


`gainSlider_` is a child of `controlBar_.hwnd_`, which is created with class `"STATIC"`. When the user moves the trackbar, Windows sends `WM_HSCROLL` to the trackbar's immediate parent window — the `STATIC` child. The default `STATIC` WndProc passes `WM_HSCROLL` to `DefWindowProc`, which discards it. `WM_HSCROLL` never propagates to `MainWindow::handleMessage`. `gainValue()` is never polled. `liveEngine_.gain` is never updated by user interaction.

### Gap 2 — Gain slider position and engine value diverge on startup

The slider default position is 80 (`create()`) but `liveEngine_.gain` initialises to `1.0f`. From the first frame of audio, slider and engine show different values. There is no gain-synchronisation step in `MainWindow::create()`.

### Gap 3 — Gain not persisted in AppSettings

`AppSettings` has no `gain` field. Gain written by preset import affects `*liveEngine_.gain` but is never written to `settings_`. On the next launch, gain resets to 1.0f regardless of any prior session.

### Gap 4 — No `ControlBar::setGainValue()` to sync slider after preset import

The preset import handler writes `*liveEngine_.gain` but has no way to update the slider thumb — `ControlBar` has no setter for slider position. After an import the slider shows stale visual state.

### Gap 5 — No safe-close guard during active recording

`WM_DESTROY` calls `settings_.save()` and `audioEngine_.stop()` but never stops the recorder. `liveEngine_.stopRecording()` is not called and `recordingFd_` is never closed. For MP3 and OGG, the codec must flush its final frames before the file is valid; without `stopRecording()`, the output file is always corrupt or truncated if the app is closed during recording.

There is no `WM_CLOSE` handler. The user is never asked to confirm closure while recording.

### Gap 6 — No Help > About menu item

`IDR_MAINMENU` in `app.rc` has `File` and `Settings` menus. There is no `Help` popup and no `IDD_ABOUT` or `IDM_HELP_ABOUT` resource. A standard Windows app is expected to have at minimum a Help > About entry.

---

## Implementation plan

### Task 1 — Add `ControlBar::setGainValue()`

**File: `src/win32/ControlBar.h`**

Add a public method signature:

```cpp
// Programmatically reposition the gain slider (clamped to [0, 100]).
void setGainValue(int pos);
```

**File: `src/win32/ControlBar.cpp`**

Implement after `gainValue()`:

```cpp
void ControlBar::setGainValue(int pos) {
    if (!gainSlider_) return;
    if (pos < 0)   pos = 0;
    if (pos > 100) pos = 100;
    SendMessage(gainSlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(pos));
}
```

No other changes in this file; this is a pure addition.

---

### Task 2 — Persist gain in AppSettings

**File: `src/win32/AppSettings.h`**

Add `float gain = 0.8f;` alongside the other fields:

```cpp
struct AppSettings {
    ...
    int   recordFormat  = 0;
    int   recordQuality = 0;
    float gain          = 0.8f;   // ← add this line
    ...
};
```

**File: `src/win32/AppSettings.cpp`**

In `save()`, add:
```cpp
j["gain"] = settings.gain;
```

In `load()`, add:
```cpp
s.gain = j.value("gain", 0.8f);
```

The default `0.8f` matches the slider default position of 80 created in `ControlBar::create()`.

---

### Task 3 — Subclass the ControlBar container to forward `WM_HSCROLL`

The root cause of Gap 1 is that `gainSlider_`'s parent is a `STATIC` window that does not forward `WM_HSCROLL`. The correct fix without restructuring the control hierarchy is to subclass the `STATIC` container window from `MainWindow::create()` using `SetWindowSubclass` (`<commctrl.h>`), which installs a per-window hook. The hook forwards `WM_HSCROLL` originating from `gainSlider_` up to the main window.

**File: `src/win32/MainWindow.h`**

Declare the static subclass proc and a private helper:

```cpp
private:
    ...
    static LRESULT CALLBACK ControlBarSubclassProc(HWND hwnd, UINT msg,
                                                   WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR subclassId,
                                                   DWORD_PTR refData);
    void onGainChanged();
```

**File: `src/win32/MainWindow.cpp`**

Add the subclass installation immediately after `controlBar_.create(hwnd_, dummy)`:

```cpp
controlBar_.create(hwnd_, dummy);
// Forward WM_HSCROLL from the ControlBar container to this window.
SetWindowSubclass(controlBar_.hwnd(), ControlBarSubclassProc,
                  1 /*subclassId*/, reinterpret_cast<DWORD_PTR>(this));
```

Implement the subclass proc (does not need to be inside the class switch; place it above `WndProc`):

```cpp
LRESULT CALLBACK MainWindow::ControlBarSubclassProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR /*subclassId*/, DWORD_PTR refData) {
    if (msg == WM_HSCROLL) {
        MainWindow* self = reinterpret_cast<MainWindow*>(refData);
        if (self)
            self->onGainChanged();
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
```

`DefSubclassProc` correctly chains to the original window proc for all other messages.

Implement `onGainChanged()`:

```cpp
void MainWindow::onGainChanged() {
    if (!liveEngine_.gain) return;
    int pos = controlBar_.gainValue();           // [0, 100]
    float g = static_cast<float>(pos) / 100.0f;
    *liveEngine_.gain  = g;
    settings_.gain     = g;                      // kept in-memory; flushed by WM_DESTROY
}
```

`settings_.save()` is already called in `WM_DESTROY`, so the value will be persisted on the next clean shutdown without requiring an explicit save on every drag event.

---

### Task 4 — Restore and sync gain at startup

**File: `src/win32/MainWindow.cpp` — in `create()`, after `controlBar_.create()` and subclass installation**

After the control bar is created and the subclass is installed, sync the slider and engine to the persisted value:

```cpp
// Sync gain slider and engine to the persisted setting.
if (liveEngine_.gain)
    *liveEngine_.gain = settings_.gain;
controlBar_.setGainValue(static_cast<int>(settings_.gain * 100.0f + 0.5f));
```

The `+ 0.5f` rounds to nearest integer rather than truncating (e.g. `0.8f * 100 = 79.999... → 79` without it).

---

### Task 5 — Sync gain slider after preset import

**File: `src/win32/MainWindow.cpp` — `IDM_FILE_IMPORT_PRESET` handler**

After the existing `*liveEngine_.gain = preset["gain"].get<float>()` assignment, add:

```cpp
float g = *liveEngine_.gain;
settings_.gain = g;
controlBar_.setGainValue(static_cast<int>(g * 100.0f + 0.5f));
```

This ensures the slider position, engine value, and in-memory settings all agree after a preset import.

---

### Task 6 — Add `WM_CLOSE` safe-recording guard

**File: `src/win32/MainWindow.cpp` — `handleMessage()` switch**

Add a `WM_CLOSE` case before `WM_DESTROY`:

```cpp
case WM_CLOSE: {
    if (recordingFd_ >= 0) {
        int choice = MessageBoxA(hwnd_,
            "A recording is in progress. Stop recording and close?",
            "Opiqo — Recording Active", MB_YESNO | MB_ICONWARNING);
        if (choice != IDYES)
            return 0;  // User cancelled — leave app running.
        // Finalise the recording before the window is destroyed.
        liveEngine_.stopRecording();
        _close(recordingFd_);
        recordingFd_ = -1;
        controlBar_.setRecordState(false);
    }
    DestroyWindow(hwnd_);
    return 0;
}
```

**File: `src/win32/MainWindow.cpp` — `WM_DESTROY` handler**

Add a safety-net check at the top of `WM_DESTROY` to handle any path that bypasses `WM_CLOSE` (e.g. `DestroyWindow` called directly):

```cpp
case WM_DESTROY:
    // Safety net: finalise any open recording (normally handled in WM_CLOSE).
    if (recordingFd_ >= 0) {
        liveEngine_.stopRecording();
        _close(recordingFd_);
        recordingFd_ = -1;
    }
    settings_.save();
    audioEngine_.stop();
    deviceEnum_.reset();
    CoUninitialize();
    PostQuitMessage(0);
    return 0;
```

Note: `settings_.save()` must remain after the safety-net block so `settings_.gain` is written to disk. If the safety-net fires for recording but `WM_CLOSE` was never processed, `settings_.gain` has still been kept current by `onGainChanged()`.

---

### Task 7 — Add Help > About menu and handler

**File: `src/win32/resource.h`**

Add:
```c
#define IDM_HELP_ABOUT  401
```

**File: `src/win32/app.rc`**

In `IDR_MAINMENU`, append a new popup after the `Settings` popup:

```rc
POPUP "&Help"
BEGIN
    MENUITEM "&About Opiqo...", IDM_HELP_ABOUT
END
```

**File: `src/win32/MainWindow.cpp` — `WM_COMMAND` switch**

Add a case alongside the other menu items:

```cpp
case IDM_HELP_ABOUT:
    MessageBoxA(hwnd_,
        "Opiqo Windows Host\nVersion 1.0\n\nLV2 audio plugin host with WASAPI duplex audio.",
        "About Opiqo", MB_OK | MB_ICONINFORMATION);
    return 0;
```

No new resource ID or dialog resource is needed; a `MessageBoxA` is sufficient for a minimal About display.

---

## File change summary

| File | Change type | Description |
|---|---|---|
| `src/win32/ControlBar.h` | Add method | `setGainValue(int)` declaration |
| `src/win32/ControlBar.cpp` | Add method | `setGainValue(int)` implementation |
| `src/win32/AppSettings.h` | Add field | `float gain = 0.8f` |
| `src/win32/AppSettings.cpp` | Extend save/load | `j["gain"]` serialisation |
| `src/win32/MainWindow.h` | Add method | `onGainChanged()`, `ControlBarSubclassProc` static |
| `src/win32/MainWindow.cpp` | Add subclass | `SetWindowSubclass` call + `ControlBarSubclassProc` impl |
| `src/win32/MainWindow.cpp` | Add method | `onGainChanged()` implementation |
| `src/win32/MainWindow.cpp` | Edit `create()` | Gain sync after controlBar setup |
| `src/win32/MainWindow.cpp` | Edit import handler | Gain slider sync after preset import |
| `src/win32/MainWindow.cpp` | Add `WM_CLOSE` case | Recording confirmation + safe file close |
| `src/win32/MainWindow.cpp` | Edit `WM_DESTROY` | Safety-net recording flush before `settings_.save()` |
| `src/win32/resource.h` | Add constant | `IDM_HELP_ABOUT 401` |
| `src/win32/app.rc` | Add menu | Help > About Opiqo |
| `src/win32/MainWindow.cpp` | Add menu handler | `IDM_HELP_ABOUT` in `WM_COMMAND` |

No new source files. No CMakeLists change required.

---

## Testing plan

### Gain slider — basic wiring
1. Launch app. Confirm slider is at 80% visually.
2. Load an audio source. Start the engine.
3. Drag slider to 0: confirm audio is silenced.
4. Drag slider to 100: confirm audio at full level.
5. Drag to 50. Close the app and relaunch. Confirm slider reopens at 50%.

### Gain slider — preset sync
1. Export a preset while slider is at 50. Manually edit the JSON to set `"gain": 0.9`.
2. Import the edited preset. Confirm slider snaps to 90%.
3. Verify `*liveEngine_.gain ≈ 0.9f` takes effect (audible level change).

### Safe close — recording active
1. Start engine. Start a WAV recording.
2. Attempt to close the window (Alt+F4 or × button).
3. Confirm a dialog appears. Click Cancel. Verify app stays open and recording continues.
4. Click × again. Click OK. Verify the app closes and the WAV file is valid (non-zero size, playable).

### Safe close — recording active, OGG/MP3
1. Repeat above with OGG and MP3 formats. Verify codec flush produces a valid file.

### About dialog
1. Click Help > About Opiqo. Verify a message box appears with the correct text. Click OK; verify it closes normally.
