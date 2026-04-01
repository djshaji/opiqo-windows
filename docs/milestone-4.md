# Milestone 4 — Power Toggle State Machine and Transport Control

## Changed files

### `src/win32/resource.h`
Two new identifiers added:

| Identifier | Value | Purpose |
|---|---|---|
| `IDC_POWER_TOGGLE` | `50100` | Child control ID for the power button in `ControlBar` |
| `IDT_ENGINE_STATE` | `1` | `WM_TIMER` ID used to poll the engine state after a start request |

### `src/win32/ControlBar.h`
- Added `powerButton_` private member (`HWND`) to track the button handle independently of the container window.
- Added `setPowerState(bool on)` public method — updates the button's checked state via `BM_SETCHECK` without posting a `WM_COMMAND` to the parent. Used by `MainWindow` to revert the toggle on start failure without re-entering the command handler.

### `src/win32/ControlBar.cpp`
Replaced the single `STATIC` placeholder:

- Container window is now a borderless `STATIC` with no text (invisible panel).
- Power button created as a child of the container using `BS_AUTOCHECKBOX | BS_PUSHLIKE` — renders as a latching pushbutton with checked/unchecked visual states.
- Button dimensions: 80 × 28 px, offset 4 px from the top-left of the bar.
- Button uses `IDC_POWER_TOGGLE` as its menu/ID parameter so `WM_COMMAND` from the parent window can identify it.
- `setPowerState()` implemented with `SendMessage(powerButton_, BM_SETCHECK, ...)`.

### `src/win32/MainWindow.h`
- Added `#include "ControlBar.h"`.
- Added `controlBar_` member (`ControlBar`).
- Added `onEngineStatePoll()` private method declaration.

### `src/win32/MainWindow.cpp`

#### `create()`
After wiring the DSP engine, creates the `ControlBar` in a 40 px strip along the bottom edge of the initial client area:
```cpp
constexpr int kBarHeight = 40;
RECT barBounds = { 0, rc.bottom - kBarHeight, rc.right, rc.bottom };
controlBar_.create(hwnd_, barBounds);
```

#### `handleMessage()` — `WM_COMMAND` additions

New case `IDC_POWER_TOGGLE`:

**Toggle ON path** (button transitions to checked):
1. Reads the new checked state with `IsDlgButtonChecked`.
2. Calls `audioEngine_.start(sampleRate, blockSize, inputDeviceId, outputDeviceId, exclusiveMode)` using the resolved `settings_` values.
3. If `start()` returns `false` (immediate refusal — wrong state or bad parameters): reverts button to OFF via `controlBar_.setPowerState(false)` and shows `errorMessage()` in a `MessageBoxA`.
4. If `start()` returns `true`: installs a 50 ms `WM_TIMER` with ID `IDT_ENGINE_STATE` to poll the async audio thread.

**Toggle OFF path** (button transitions to unchecked):
1. Calls `audioEngine_.stop()` (synchronous — blocks until the audio thread joins).
2. Explicitly calls `controlBar_.setPowerState(false)` to guarantee the button reflects the Off state regardless of any race.

#### `handleMessage()` — `WM_TIMER` additions

New case `WM_TIMER` dispatches `IDT_ENGINE_STATE` to `onEngineStatePoll()`.

#### `onEngineStatePoll()` (new method)
Called every 50 ms after a successful `start()` launch:

```
state == Starting  → return early (keep polling)
state == Running   → KillTimer; setPowerState(true)   // confirm ON
state == anything else
                   → KillTimer; setPowerState(false);
                     show errorMessage() in MessageBoxA // revert to OFF
```

This ensures the toggle stays ON only after the audio thread has confirmed it reached `Running`, and reverts cleanly to OFF on any failure that occurs asynchronously inside `audioThreadProc()`.
