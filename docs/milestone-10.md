# Milestone 10 — Hardening, Performance, and Release Readiness

## What the milestone requires

- Rate-limited error logging and user-facing status messages.
- MMCSS priority setup for the audio thread.
- Long-run soak tests and device hot-plug stress tests.
- Verify no blocking operations in the audio loop.

Acceptance criteria:
- 60-minute continuous run without dropout under normal load.
- Device loss/recovery path works without app restart.

---

## What already existed (usable as-is)

| Component | Status |
|---|---|
| MMCSS thread priority | **Done.** `AvSetMmThreadCharacteristicsW(L"Pro Audio")` and `AvRevertMmThreadCharacteristics` already called in `AudioEngine::audioThreadProc()`. `avrt` already linked. No work needed. |
| `AudioEngine::State` machine | `Off / Starting / Running / Stopping / Error` defined and used in `start()`, `stop()`, and `audioThreadProc()`. `AUDCLNT_E_DEVICE_INVALIDATED` in `runLoop()` already transitions to `State::Error`. |
| `WasapiDeviceEnum` hot-plug | `IMMNotificationClient` fires → `PostMessage(hwnd, WM_OPIQO_DEVICE_CHANGE)` → `onDeviceListChanged()`. Device list refresh and `resolveOrDefault()` fallback already complete. |
| `IDT_ENGINE_STATE` poll | 50 ms timer started on `start()`, killed when `Running` or `Error` confirmed in `onEngineStatePoll()`. |
| Pre-allocated scratch buffers | `inBuf`, `outBuf`, `stereoBuf` allocated at stream start; reused in the hot loop. No heap allocation in `runLoop()`. |

---

## What was implemented

### `src/win32/win_logging.h` (new file)

- Declared `opiqo_log_init()`, `opiqo_log_shutdown()`, and `opiqo_win_log(file, line, level, fmt, ...)` with C linkage.

### `src/win32/win_logging.cpp` (new file)

- `opiqo_log_init()`: resolves `%APPDATA%\Opiqo\` via `GetEnvironmentVariableA("APPDATA")`, creates the directory with `CreateDirectoryA`, opens `opiqo.log` for append. Failure is non-fatal — output still goes to `OutputDebugStringA`.
- `opiqo_win_log()`: rate-limited per call site using a `std::unordered_map<uint64_t, ULONGLONG>` keyed on FNV-1a hash of `(file, line)`. Calls within 1 000 ms of the first occurrence from the same site are suppressed. Surviving calls are formatted with a `[LEVEL basename:line]` prefix, sent to `OutputDebugStringA`, and written + flushed to the log file.
- `opiqo_log_shutdown()`: flushes and closes the log file.
- All state guarded by a `std::mutex`; rate-limit table entries checked and written under the same lock.

### `src/logging_macros.h`

- Added `#ifdef _WIN32` branch that defines all `LOG*` macros as calls to `opiqo_win_log(__FILE__, __LINE__, "level", ...)`.
- `#else` block preserves the original `printf` macros for Linux/Android builds unchanged.

### `src/main_win32.cpp`

- Added `#include "win32/win_logging.h"`.
- `opiqo_log_init()` called at the top of `WinMain`, before `MainWindow::create()`.
- `opiqo_log_shutdown()` called before every return path (both failure and normal exit).

### `src/win32/resource.h`

- Added `IDT_ENGINE_WATCHDOG 2` alongside the existing `IDT_ENGINE_STATE 1`.

### `src/win32/MainWindow.h`

- Added private method `void onEngineError()`.

### `src/win32/MainWindow.cpp`

- **`onEngineStatePoll()`**: after confirming `State::Running`, starts a 500 ms `IDT_ENGINE_WATCHDOG` timer to detect mid-session device loss.
- **`WM_TIMER` — `IDT_ENGINE_WATCHDOG` branch**:
  - If `audioEngine_.state() == State::Error`: kills the watchdog timer and calls `onEngineError()`.
  - Otherwise: reads `audioEngine_.dropoutCount()`, rebuilds the output device friendly name from `enumerateOutputDevices()`, appends `"  [N dropout(s)]"` if count > 0, and updates status bar part 1 via `SB_SETTEXTA`.
- **`onEngineError()` (new method)**:
  - If recording is active: calls `liveEngine_.stopRecording()`, `_close(recordingFd_)`, resets `recordingFd_ = -1`, calls `controlBar_.setRecordState(false)`.
  - Resets power toggle and record button to off/disabled.
  - Writes `"Error: <message>"` into status bar part 0.
  - Shows a `MB_YESNO` `MessageBoxA` offering to restart on the default device.
  - On Yes: calls `onDeviceListChanged()` to resolve new default, calls `audioEngine_.start(...)`, on success sets toggle ON and starts `IDT_ENGINE_STATE`; on failure shows error and leaves toggle OFF.
- **`onDeviceListChanged()`**: extended — after updating the status bar, checks if `audioEngine_.state() == State::Error`; if so, kills `IDT_ENGINE_WATCHDOG` and calls `onEngineError()`. This makes recovery deterministic regardless of whether the watchdog timer or `IMMNotificationClient` fires first.
- **Manual stop path (`IDC_POWER_TOGGLE` off branch)**: added `KillTimer(hwnd_, IDT_ENGINE_WATCHDOG)` before resetting UI state.
- **Settings apply path (`IDM_SETTINGS_OPEN`)**: added `KillTimer(hwnd_, IDT_ENGINE_WATCHDOG)` in the `if (wasRunning)` stop block.

### `src/LiveEffectEngine.h`

- Added `#include <atomic>`.
- Changed `bool bypass = false` to `std::atomic<bool> bypass { false }`, eliminating the data race between the UI thread (writer in `addPlugin`/`deletePlugin`) and the audio thread (reader in `process()`).

### `src/LiveEffectEngine.cpp` — `process()`

- Replaced the blocking `std::lock_guard<std::mutex> lock(pluginMutex)` with a two-stage non-blocking guard:
  1. `bypass.load(std::memory_order_acquire)` fast-path: if set, `memcpy` input → output and return immediately.
  2. `pluginMutex.try_lock()` fallback: if the mutex is held by the UI thread, `memcpy` input → output and return. On success, takes ownership with `std::lock_guard(pluginMutex, std::adopt_lock)`.
- The audio thread can no longer block on any mutex call in `process()`.
- `addPlugin` and `deletePlugin` are unchanged — they set `bypass = true` (now an atomic store) before taking the mutex, ensuring the audio thread takes the fast-path during LV2 instantiation.

### `src/win32/AudioEngine.h`

- Added `uint64_t dropoutCount() const`.

### `src/win32/AudioEngine.cpp`

- `Impl` struct: added `std::atomic<uint64_t> dropouts { 0 }`.
- `audioThreadProc()`: added `impl_->dropouts.store(0, std::memory_order_relaxed)` immediately before `impl_->state = State::Running` — resets the counter for each new session.
- `runLoop()`: after reading `captureFlags`, added `if (captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) impl_->dropouts.fetch_add(1, std::memory_order_relaxed)`.
- `dropoutCount()` accessor: `return impl_->dropouts.load(std::memory_order_relaxed)`.

### `CMakeLists.txt`

- Added `src/win32/win_logging.cpp` to `WIN32_UI_SOURCES`.

---

## Soak and stress test plan

### 60-minute soak test

Manual procedure:
1. Launch the app. Select valid input/output devices.
2. Load four plugins (one per slot).
3. Enable the power toggle. Confirm `State::Running`.
4. Leave running for 60 minutes with no user interaction.
5. At end: read dropout count from status bar. 0 dropouts = pass.

### Device hot-plug stress test

Manual procedure:
1. Start engine with a USB audio interface as the input device.
2. Unplug the USB interface while the engine is running.
3. Verify: watchdog fires within 500 ms, power toggle resets, error text appears in status bar part 0, dialog offers restart.
4. Click Yes. Verify: engine restarts on the system default device, power toggle turns back ON, `IDT_ENGINE_WATCHDOG` resumes.
5. Replug the USB interface. Verify: `WM_OPIQO_DEVICE_CHANGE` fires, status bar refreshes with the USB device name. Engine continues on the default device without crash.
6. Repeat steps 2–5 five times in rapid succession without closing the app.

Pass criterion: app remains responsive after all five cycles with no crash, hang, or unreleased WASAPI handle.

### Rapid power toggle stress test

Re-run the M4 test:
1. Toggle power ON/OFF 50 times without pausing.
2. Verify no leaked WASAPI handles, no crash, `State` is `Off` after the final OFF.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| Log directory | `%APPDATA%\Opiqo\opiqo.log` — same folder as `settings.json`. Used `GetEnvironmentVariableA` instead of `SHGetFolderPathA` to avoid the `shlobj` WINVER requirement in MinGW. |
| Rate-limiting scope | Per call-site `(file, line)` FNV-1a hash, 1 s cooldown. More precise than per-message-string hashing and cheaper to compute. |
| Watchdog vs. PostMessage from audio thread | Watchdog timer on UI thread chosen. Keeps `AudioEngine` decoupled from any window handle. |
| `bypass` atomicity | Changed to `std::atomic<bool>` with `memory_order_acquire` read in `process()`. Eliminates the data race; existing `bypass = true/false` assignments in `addPlugin`/`deletePlugin` compile correctly as atomic stores via `operator=`. |
| try_lock fallback audio | On a missed lock, audio is passed through unprocessed (memcpy) rather than silenced — a brief unprocessed burst is less disruptive than a dropout. |
| Dropout counter reset | Reset in `audioThreadProc()` on each `start()` so the counter reflects the current session only. |
| Watchdog kill on manual stop | `KillTimer(IDT_ENGINE_WATCHDOG)` added to both the power-toggle off path and the settings-apply stop path so the watchdog never fires against a deliberately stopped engine. |
| `onDeviceListChanged` + `onEngineError` ordering | Both paths call `KillTimer(IDT_ENGINE_WATCHDOG)` before `onEngineError()`. `KillTimer` with an inactive ID is a safe no-op. |
| Recording on device loss | `stopRecording()` called before `_close(recordingFd_)` in `onEngineError()`, matching the safe shutdown order from M8. |


| Component | Status |
|---|---|
| MMCSS thread priority | **Done.** `AvSetMmThreadCharacteristicsW(L"Pro Audio")` and `AvRevertMmThreadCharacteristics` are called in `AudioEngine::audioThreadProc()`. `avrt` is already linked. No work needed. |
| `AudioEngine::State` machine | `Off / Starting / Running / Stopping / Error` defined and used correctly in `start()`, `stop()`, and `audioThreadProc()`. Device invalidation sets `State::Error` via `AUDCLNT_E_DEVICE_INVALIDATED` in `runLoop()`. |
| `WasapiDeviceEnum` hot-plug | `IMMNotificationClient` fires → `PostMessage(hwnd, WM_OPIQO_DEVICE_CHANGE)` → `onDeviceListChanged()`. The device list refresh and `resolveOrDefault()` fallback are complete. |
| `IDT_ENGINE_STATE` poll | 50 ms timer started on `start()`, killed when `Running` or `Error` is confirmed in `onEngineStatePoll()`. |
| Pre-allocated scratch buffers | `inBuf`, `outBuf`, `stereoBuf` allocated at stream start; reused in the hot loop. No heap allocation in `runLoop()`. |
| `bypass` flag | `bool bypass` in `LiveEffectEngine` gates processing during `addPlugin` / `deletePlugin`. Currently a plain (non-atomic) `bool`. |

---

## Gaps requiring implementation

### Gap 1 — No Windows-visible logging

`logging_macros.h` routes all log output to `printf`. On a Windows desktop app launched without a console, this output is discarded entirely. Errors and warnings are invisible at runtime and during QA. Additionally, there is no rate-limiting: a device error that fires every audio callback (~5 ms) would emit thousands of identical lines per second if a log sink were present.

### Gap 2 — No runtime detection of engine dropping into Error

The `IDT_ENGINE_STATE` timer is killed the moment the engine confirms `State::Running`. From that point no periodic check is running on the UI thread. If the audio thread later sets `State::Error` (device pulled mid-session), the UI is never notified: the power toggle stays ON, the status bar is stale, and the record button remains enabled.

### Gap 3 — Device-loss recovery path is incomplete

`onDeviceListChanged()` refreshes the status bar and resolves saved device IDs against the current device list. It does not inspect the engine state. If the engine is already in `State::Error` (device was pulled) when this handler runs, no recovery attempt is made. The acceptance criterion "device loss/recovery without app restart" is not met.

### Gap 4 — Blocking mutex in the real-time audio loop

`LiveEffectEngine::process()` acquires `pluginMutex` for the duration of every audio callback. `addPlugin` and `deletePlugin` also acquire the same mutex and hold it for the entire LV2 instantiation and initialization sequence (potentially tens to hundreds of milliseconds). If a user adds or removes a plugin while the engine is `Running`, the audio thread blocks waiting for the mutex, causing a guaranteed glitch or WASAPI buffer underrun.

`bypass` is written from the UI thread and read from the audio thread as a plain `bool` with no memory ordering guarantee — this is a data race.

### Gap 5 — No dropout visibility or soak test instrumentation

There is no counter for `AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY` flags (WASAPI's signal that capture frames were dropped). Without this, QA cannot verify the 60-minute dropout-free criterion quantitatively.

---

## Implementation plan

### Task 1 — Windows rate-limited logging

**New files:** `src/win32/win_logging.h`, `src/win32/win_logging.cpp`

#### `src/win32/win_logging.h`

Declare one function:

```cpp
// Must be called once before any log macro fires (e.g. at the top of WinMain).
void opiqo_log_init();

// Must be called at shutdown.
void opiqo_log_shutdown();

// Internal — called by the LOG* macros. Not for direct use.
void opiqo_win_log(const char* file, int line, const char* level, const char* fmt, ...);
```

#### `src/win32/win_logging.cpp`

- Open `%APPDATA%\Opiqo\opiqo.log` for append in `opiqo_log_init()`. Store the `FILE*` in a static variable. Create the directory with `SHGetFolderPathA(CSIDL_APPDATA)` + `CreateDirectoryA` if it does not exist.
- `opiqo_win_log()`:
  - Rate-limit: maintain a `static std::unordered_map<uint64_t, ULONGLONG> lastSeen` keyed by `(hash of file string) ^ (line << 16)`. Suppress repeat entries from the same call site within 1 000 ms using `GetTickCount64()`. Always allow the first occurrence.
  - Format the message into a local `char[512]` with `vsnprintf`.
  - Prepend `[level file:line]` prefix.
  - Write to the log file (with `fflush`) and to `OutputDebugStringA`.
- `opiqo_log_shutdown()`: `fflush` and `fclose` the log file.

#### `src/logging_macros.h`

Add a `#ifdef _WIN32` branch to redirect the existing macros:

```cpp
#ifdef _WIN32
  // Declared in src/win32/win_logging.h — included by the Windows build.
  void opiqo_win_log(const char* file, int line, const char* level, const char* fmt, ...);
  #define LOGV(...) opiqo_win_log(__FILE__, __LINE__, "V", __VA_ARGS__)
  #define LOGD(...) opiqo_win_log(__FILE__, __LINE__, "D", __VA_ARGS__)
  #define LOGI(...) opiqo_win_log(__FILE__, __LINE__, "I", __VA_ARGS__)
  #define LOGW(...) opiqo_win_log(__FILE__, __LINE__, "W", __VA_ARGS__)
  #define LOGE(...) opiqo_win_log(__FILE__, __LINE__, "E", __VA_ARGS__)
  #define LOGF(...) opiqo_win_log(__FILE__, __LINE__, "F", __VA_ARGS__)
#else
  // … existing printf macros unchanged …
#endif
```

The `#else` block keeps the Android/Linux build unaffected.

#### `src/main_win32.cpp`

Call `opiqo_log_init()` immediately after `WinMain` entry (before `MainWindow::create`) and `opiqo_log_shutdown()` after the message loop returns.

#### `CMakeLists.txt`

Add `src/win32/win_logging.cpp` to the Windows target source list.

---

### Task 2 — Engine watchdog timer

**Modified files:** `src/win32/resource.h`, `src/win32/MainWindow.h`, `src/win32/MainWindow.cpp`

#### `src/win32/resource.h`

Add:

```cpp
#define IDT_ENGINE_WATCHDOG  2
```

(IDT_ENGINE_STATE is already 1.)

#### `src/win32/MainWindow.h`

Add a private method:

```cpp
// Called when the watchdog detects the engine has entered State::Error mid-session.
void onEngineError();
```

#### `src/win32/MainWindow.cpp` — `onEngineStatePoll()`

After confirming `State::Running`, start the watchdog timer:

```cpp
if (s == AudioEngine::State::Running) {
    controlBar_.setPowerState(true);
    controlBar_.enableRecordButton(true);
    // Start a long-period watchdog to detect mid-session device loss.
    SetTimer(hwnd_, IDT_ENGINE_WATCHDOG, 500, nullptr);
}
```

#### `src/win32/MainWindow.cpp` — `WM_TIMER` handler

Add an `IDT_ENGINE_WATCHDOG` branch:

```cpp
case IDT_ENGINE_WATCHDOG:
    if (audioEngine_.state() == AudioEngine::State::Error)
        onEngineError();
    return 0;
```

#### `src/win32/MainWindow.cpp` — `onEngineError()` implementation

```
void MainWindow::onEngineError() {
    KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);

    // Stop any active recording before communicating with the user.
    if (recordingFd_ >= 0) {
        liveEngine_.stopRecording();
        _close(recordingFd_);
        recordingFd_ = -1;
        controlBar_.setRecordState(false);
    }

    controlBar_.setPowerState(false);
    controlBar_.enableRecordButton(false);

    // Show the error in the status bar left part.
    std::string msg = audioEngine_.errorMessage();
    if (msg.empty()) msg = "Audio device lost.";
    if (statusBar_)
        SendMessageA(statusBar_, SB_SETTEXTA, 0,
                     reinterpret_cast<LPARAM>(("Error: " + msg).c_str()));

    // Offer the user a chance to restart with a resolved device.
    int choice = MessageBoxA(hwnd_,
        (msg + "\n\nAttempt to restart with the default device?").c_str(),
        "Opiqo — Audio Error", MB_YESNO | MB_ICONWARNING);

    if (choice == IDYES) {
        onDeviceListChanged();  // Resolve to new default.
        bool launched = audioEngine_.start(
            settings_.sampleRate,
            settings_.blockSize,
            settings_.inputDeviceId,
            settings_.outputDeviceId,
            settings_.exclusiveMode);
        if (!launched) {
            controlBar_.setPowerState(false);
            MessageBoxA(hwnd_, audioEngine_.errorMessage().c_str(),
                        "Opiqo — Engine Error", MB_OK | MB_ICONERROR);
        } else {
            controlBar_.setPowerState(true);
            SetTimer(hwnd_, IDT_ENGINE_STATE, 50, nullptr);
        }
    }
}
```

---

### Task 3 — Device-loss recovery in `onDeviceListChanged()`

**Modified file:** `src/win32/MainWindow.cpp`

The current `onDeviceListChanged()` ends after updating the status bar. Extend it to handle the case where the device change event arrives before or instead of the watchdog:

```cpp
void MainWindow::onDeviceListChanged() {
    // … existing refresh and resolveOrDefault logic …

    // If the engine entered Error state (device pulled), trigger recovery flow.
    if (audioEngine_.state() == AudioEngine::State::Error) {
        // Ensure the watchdog does not double-fire.
        KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);
        onEngineError();
    }
}
```

This makes recovery deterministic regardless of whether the watchdog or `IMMNotificationClient` fires first.

---

### Task 4 — Real-time safety: atomic bypass and try-lock in process()

**Modified files:** `src/LiveEffectEngine.h`, `src/LiveEffectEngine.cpp`

#### `src/LiveEffectEngine.h`

Change the `bypass` field from `bool` to `std::atomic<bool>`:

```cpp
std::atomic<bool> bypass { false };
```

Add `#include <atomic>` if not already present (it is, via the existing `std::mutex` include chain — but add explicitly for clarity).

#### `src/LiveEffectEngine.cpp` — `process()`

Replace the blocking `lock_guard` with a non-blocking try-lock guarded by the bypass check:

```cpp
int LiveEffectEngine::process(float* input, float* output, int frames) {
    queueManager.process(input, output, frames);

    // Fast-path: UI thread is mid-mutation — pass audio through untouched.
    if (bypass.load(std::memory_order_acquire)) {
        std::memcpy(output, input, static_cast<size_t>(frames) * 2 * sizeof(float));
        return 0;
    }

    // Non-blocking acquire. If the UI thread holds pluginMutex (setValue,
    // setPluginEnabled during a non-bypass path), skip this callback rather
    // than stalling the audio thread.
    if (!pluginMutex.try_lock()) {
        std::memcpy(output, input, static_cast<size_t>(frames) * 2 * sizeof(float));
        return 0;
    }
    std::lock_guard<std::mutex> lock(pluginMutex, std::adopt_lock);

    // … existing plugin1–plugin4 processing unchanged …
}
```

`addPlugin` and `deletePlugin` remain unchanged — they still set `bypass = true` before acquiring the mutex, which ensures the audio thread takes the fast-path memcpy during the potentially long LV2 initialization window.

`setValue` and `setPluginEnabled` do not set `bypass` (they are fast, allocation-free operations). The try_lock fallback handles the rare race where those coincide with a process() call.

---

### Task 5 — Dropout counter and status bar display

**Modified files:** `src/win32/AudioEngine.h`, `src/win32/AudioEngine.cpp`, `src/win32/MainWindow.cpp`

#### `src/win32/AudioEngine.h`

Add a public accessor:

```cpp
uint64_t dropoutCount() const;
```

#### `src/win32/AudioEngine.cpp` — `Impl` struct

Add a counter field:

```cpp
std::atomic<uint64_t> dropouts { 0 };
```

#### `src/win32/AudioEngine.cpp` — `runLoop()`

In the capture packet loop, after retrieving `captureFlags`:

```cpp
if (captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
    impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
```

Implement the accessor:

```cpp
uint64_t AudioEngine::dropoutCount() const {
    return impl_->dropouts.load(std::memory_order_relaxed);
}
```

Reset on `start()`: add `impl_->dropouts.store(0)` at the top of `audioThreadProc()` before `State::Running` is set.

#### `src/win32/MainWindow.cpp` — `IDT_ENGINE_WATCHDOG` handler

Augment the watchdog to also refresh the dropout display every 500 ms:

```cpp
case IDT_ENGINE_WATCHDOG:
    if (audioEngine_.state() == AudioEngine::State::Error) {
        onEngineError();
    } else if (statusBar_) {
        uint64_t d = audioEngine_.dropoutCount();
        std::string outText = "Out: ";
        // Rebuild friendly name from current settings.
        auto outputs = deviceEnum_->enumerateOutputDevices();
        for (const auto& dev : outputs)
            if (dev.id == settings_.outputDeviceId) { outText += dev.friendlyName; break; }
        if (d > 0)
            outText += "  [" + std::to_string(d) + " dropout" + (d == 1 ? "" : "s") + "]";
        SendMessageA(statusBar_, SB_SETTEXTA, 1,
                     reinterpret_cast<LPARAM>(outText.c_str()));
    }
    return 0;
```

---

### Task 6 — CMakeLists.txt

**Modified file:** `CMakeLists.txt`

Add `src/win32/win_logging.cpp` to the Windows target. Verify (do not add twice if already present from including the whole `src/win32/` glob):

```cmake
target_sources(opiqo PRIVATE
    src/win32/win_logging.cpp
)
```

Confirm `avrt` is linked (it is in the current build); no change required unless the entry is missing:

```cmake
target_link_libraries(opiqo PRIVATE avrt)
```

---

## Soak and stress test plan

### 60-minute soak test

Manual procedure:
1. Launch the app. Select valid input/output devices.
2. Load four plugins (one per slot).
3. Enable the power toggle. Confirm `State::Running`.
4. Leave running for 60 minutes with no user interaction.
5. At end: read dropout count from status bar. 0 dropouts = pass. Any dropout = investigate buffer sizing or system load.

Automated proxy: if a CI environment is available, use a loopback virtual device and run the process under a timing harness that reads `audioEngine_.dropoutCount()` via a named-pipe IPC or a debug output reader at T=60 min.

### Device hot-plug stress test

Manual procedure:
1. Start engine with a USB audio interface as the input device.
2. Unplug the USB interface while the engine is running.
3. Verify: watchdog fires within 500 ms, power toggle resets, error text appears in status bar, dialog offers restart.
4. Click Yes. Verify: engine restarts on the system default device, power toggle turns back ON.
5. Replug the USB interface. Verify: `WM_OPIQO_DEVICE_CHANGE` fires, status bar refreshes with the USB device name. Engine continues on the default device without crash.
6. Repeat steps 2–5 five times in rapid succession without closing the app.

Pass criterion: app remains responsive and usable after all five cycles with no crash, hang, or unreleased WASAPI handle (verify with Task Manager / Resource Monitor handle count).

### Rapid power toggle stress test

No new code required (already covered in the M4 acceptance criteria test). Re-run:
1. Toggle power ON/OFF 50 times without pausing.
2. Verify no leaked WASAPI handles, no crash, `State` is `Off` after the 50th OFF.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| Watchdog vs. PostMessage from audio thread | Watchdog timer on the UI thread chosen. Keeps `AudioEngine` decoupled from any window handle; no change to `AudioEngine` API needed for the detection path. |
| Rate-limiting scope | Per call-site `(file, line)` hash. This is more precise than per-message-string hashing and cheaper to compute at runtime. |
| Log file path | `%APPDATA%\Opiqo\opiqo.log`. Same folder as `settings.json` established in M1. No new directory creation logic needed. |
| `bypass` atomicity | Changed to `std::atomic<bool>` with `memory_order_acquire/release`. Eliminates the data race flagged between `addPlugin` (UI thread writer) and `process()` (audio thread reader). |
| try_lock fallback | On a missed lock in `process()`, audio is passed through unprocessed (memcpy) rather than silenced. This produces a brief unprocessed frame burst, which is less disruptive than silence. |
| Dropout counter reset | Reset in `audioThreadProc()` on each `start()` call so the counter reflects the current session only. Persistent totals can be inferred from the log file. |
| `onDeviceListChanged` + `onEngineError` ordering | Both paths call `KillTimer(IDT_ENGINE_WATCHDOG)` before `onEngineError`. `KillTimer` is safe to call with an ID that is not active — it is a no-op in that case. |
| Recording on device loss | `stopRecording()` is called before `_close(recordingFd_)` in `onEngineError()`, matching the safe shutdown order established in M8. |
