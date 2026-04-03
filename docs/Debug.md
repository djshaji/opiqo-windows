# Debug Menu

## Feature Summary

A **Debug** top-level menu with a **Stress Test: Plugin Load/Unload** item.
When activated it loops through every discovered LV2 plugin, loads each one
into slot 1, then deletes it, indefinitely.  Each action is timestamped in the
log.  The item acts as a toggle: clicking it a second time stops the test.

---

## Files Changed

| File | Change |
|---|---|
| `src/win32/resource.h` | Added `IDM_DEBUG_STRESS_TEST 40010` and `IDT_STRESS_TEST 3` |
| `src/win32/app.rc` | Added `POPUP "&Debug"` to `IDR_MAINMENU` |
| `src/win32/MainWindow.h` | Added `#include <vector>`, `<string>`; stress-test state members and helper declarations |
| `src/win32/MainWindow.cpp` | Implemented `startStressTest`, `stopStressTest`, `stressTestTick`; wired into `WM_COMMAND` and `WM_TIMER` |

---

## Step 1 — `resource.h`

Add two constants after the existing timer IDs:

```c
#define IDM_DEBUG_STRESS_TEST  40010
#define IDT_STRESS_TEST        3
```

---

## Step 2 — `app.rc`

Append a Debug popup to `IDR_MAINMENU`, after the `Settings` popup:

```rc
POPUP "&Debug"
BEGIN
    MENUITEM "Stress Test: Plugin Load/Unload", IDM_DEBUG_STRESS_TEST
END
```

---

## Step 3 — `MainWindow.h`

Add private members and helpers:

```cpp
// --- Stress-test state ---
void startStressTest();
void stopStressTest();
void stressTestTick();          // called each timer fire

std::vector<std::string> stressUris_;   // all available plugin URIs
int                      stressIndex_  = 0;
bool                     stressAdded_  = false;  // true when a plugin is loaded
bool                     stressActive_ = false;
```

---

## Step 4 — `MainWindow.cpp`

### 4a. Handle `IDM_DEBUG_STRESS_TEST` in `WM_COMMAND`

```cpp
case IDM_DEBUG_STRESS_TEST: {
    if (stressActive_) {
        stopStressTest();
    } else {
        startStressTest();
    }
    return 0;
}
```

### 4b. Handle `IDT_STRESS_TEST` in `WM_TIMER`

```cpp
if (wParam == IDT_STRESS_TEST) {
    stressTestTick();
    return 0;
}
```

### 4c. `startStressTest()`

```cpp
void MainWindow::startStressTest() {
    // Collect all plugin URIs from the engine.
    json all = liveEngine_.getAvailablePlugins();
    stressUris_.clear();
    for (auto it = all.begin(); it != all.end(); ++it)
        stressUris_.push_back(it.key());

    if (stressUris_.empty()) {
        MessageBoxA(hwnd_, "No plugins found — nothing to stress test.",
                    "Debug", MB_OK | MB_ICONINFORMATION);
        return;
    }

    stressIndex_  = 0;
    stressAdded_  = false;
    stressActive_ = true;

    // Check the menu item so the user can see the test is running.
    HMENU hMenu = GetMenu(hwnd_);
    if (hMenu)
        CheckMenuItem(hMenu, IDM_DEBUG_STRESS_TEST, MF_BYCOMMAND | MF_CHECKED);

    LOG_INFO("StressTest started — %zu plugins", stressUris_.size());
    SetTimer(hwnd_, IDT_STRESS_TEST, 100 /*ms*/, nullptr);
}
```

### 4d. `stopStressTest()`

```cpp
void MainWindow::stopStressTest() {
    KillTimer(hwnd_, IDT_STRESS_TEST);
    stressActive_ = false;

    // Clean up slot 1 if a plugin is still loaded.
    if (stressAdded_) {
        liveEngine_.deletePlugin(1);
        slots_[0].clearParameterPanel();
        slots_[0].clearPlugin();
        stressAdded_ = false;
    }

    HMENU hMenu = GetMenu(hwnd_);
    if (hMenu)
        CheckMenuItem(hMenu, IDM_DEBUG_STRESS_TEST, MF_BYCOMMAND | MF_UNCHECKED);

    LOG_INFO("StressTest stopped after %d iterations",
             stressIndex_ / static_cast<int>(stressUris_.size() > 0 ? stressUris_.size() : 1));
}
```

### 4e. `stressTestTick()`

Alternates between an **add** phase and a **delete** phase on each timer fire:

```cpp
void MainWindow::stressTestTick() {
    if (stressUris_.empty()) { stopStressTest(); return; }

    const std::string& uri = stressUris_[stressIndex_];

    if (!stressAdded_) {
        // --- ADD phase ---
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        LOG_INFO("[%02d:%02d:%02d.%03d] StressTest ADD  [%d] %s",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 stressIndex_, uri.c_str());

        int result = liveEngine_.addPlugin(1, uri);
        if (result == 0) {
            json all = liveEngine_.getAvailablePlugins();
            std::string name = uri;
            if (all.contains(uri) && all[uri].contains("name"))
                name = all[uri]["name"].get<std::string>();
            slots_[0].setPlugin(name.c_str());
            slots_[0].buildParameterPanel(&liveEngine_);
            stressAdded_ = true;
        } else {
            LOG_WARN("[StressTest] addPlugin failed (result=%d) for %s", result, uri.c_str());
            // Advance index and stay in add phase for next tick.
            stressIndex_ = (stressIndex_ + 1) % static_cast<int>(stressUris_.size());
        }
    } else {
        // --- DELETE phase ---
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        LOG_INFO("[%02d:%02d:%02d.%03d] StressTest DEL  [%d] %s",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 stressIndex_, uri.c_str());

        liveEngine_.deletePlugin(1);
        slots_[0].clearParameterPanel();
        slots_[0].clearPlugin();
        stressAdded_ = false;

        // Advance to the next plugin (wrap around).
        stressIndex_ = (stressIndex_ + 1) % static_cast<int>(stressUris_.size());
    }
}
```

---

## Timer Interval

100 ms per phase (add = 100 ms, delete = 100 ms, so ≈5 plugins/second).
This can be tuned in `startStressTest()`.

---

## Log Output

Each ADD and DELETE line written via `LOG_INFO` appears in
`%APPDATA%\Opiqo\opiqo.log` and in `OutputDebugStringA` (visible in
DebugView / Visual Studio Output).  Format:

```
[HH:MM:SS.mmm] StressTest ADD  [<index>] <uri>
[HH:MM:SS.mmm] StressTest DEL  [<index>] <uri>
```

---

## Order of Implementation

1. `resource.h` — add IDs  
2. `app.rc` — add Debug menu  
3. `MainWindow.h` — add state members and declarations  
4. `MainWindow.cpp` — implement `startStressTest`, `stopStressTest`, `stressTestTick`; wire into `WM_COMMAND` and `WM_TIMER`