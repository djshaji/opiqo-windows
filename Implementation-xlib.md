# Opiqo Linux Host Implementation Plan (Xlib + JACK)

## 1. Objective

Build a native Linux port of the Opiqo LV2 plugin host using raw X11 (Xlib)
for the UI and the JACK Audio Connection Kit for audio that:
- runs low-latency duplex audio via JACK,
- processes audio through the existing `LiveEffectEngine` in real time,
- provides a hand-built X11 UI with 4 plugin slots in a 2×2 layout,
- supports JACK port routing, transport control, and recording.

Primary runtime path:
```
JACK capture ports → LiveEffectEngine::process(input, output, frames) → JACK playback ports
```

The DSP and file-writing layer (`LiveEffectEngine`, `LV2Plugin`, `FileWriter`,
`LockFreeQueue`) is shared verbatim with the Windows build.  Only the audio
platform and UI layers need a new Linux-specific implementation.

---

## 2. Technology Choices

| Concern | Windows | Linux |
|---------|---------|-------|
| UI toolkit | Win32 API | Xlib (libX11) + Xft (font rendering) |
| Widget layer | Win32 controls | Hand-built widget toolkit (`src/linux/ui/`) |
| Drawing | GDI | Xlib GC primitives + XFT text |
| Audio backend | WASAPI | JACK (libjack2) |
| Device discovery | IMMDeviceEnumerator (COM) | `jack_get_ports()` |
| Hot-plug events | IMMNotificationClient | `jack_set_port_registration_callback()` |
| Settings storage | `%APPDATA%\Opiqo\settings.json` | `~/.config/opiqo/settings.json` (XDG) |
| Build | MinGW cross-compile | Native CMake, pkg-config |

### Why Xlib instead of a toolkit?

- Xlib is the lowest-level X11 binding available on every Linux/Unix system
  without any toolkit dependency beyond `libX11`.
- A custom widget layer gives full control over rendering, layout, and event
  dispatch — matching the level of control the Win32 port has over its UI.
- The result runs on any display server that supports X11 (including Xwayland)
  with no toolkit runtime requirements.

### Companion libraries used

| Library | Purpose |
|---------|---------|
| `libX11` | Core Xlib window and drawing primitives |
| `libXft` | Anti-aliased text rendering via FreeType fonts |
| `libXrender` | Required by Xft for alpha compositing |
| `libXext` | Double-buffering extension (`XdbeSwapBuffers`) for flicker-free redraws |
| `libjack2` | JACK audio client |

---

## 3. Custom Widget Layer Design

Xlib has no built-in widgets.  A minimal toolkit must be implemented in
`src/linux/ui/` before any application-level UI work begins.

### Widget hierarchy

```
Widget (base)
  Window        ← top-level XCreateWindow wrapper
  Button        ← click, press/release states, label
  ToggleButton   ← latched Button
  Label         ← static XftDraw text
  HSlider       ← horizontal draggable thumb
  ComboBox      ← popup list window for dropdown selection
  Frame         ← bordered container with optional title label
  VBox / HBox   ← linear layout containers
  Table         ← row × col grid layout
  ScrollView    ← clipping viewport + vertical scrollbar
  TextEntry     ← single-line editable text with KeySym mapping
  Dialog        ← modal sub-window
  ListView      ← scrollable item list with keyboard navigation
```

### Widget base interface (`src/linux/ui/Widget.h`)

```cpp
struct Rect { int x, y, w, h; };

class Widget {
public:
    virtual ~Widget() = default;
    virtual void create(Display* dpy, Window parent, const Rect& r);
    virtual void show();
    virtual void hide();
    virtual void resize(const Rect& r);
    virtual void draw();                          // called on Expose
    virtual bool handleEvent(XEvent& ev);         // false = not consumed
    Window xwindow() const { return window_; }
protected:
    Display* dpy_    = nullptr;
    Window   window_ = None;
    Rect     rect_   = {};
    GC       gc_     = None;
    XftDraw* xft_    = nullptr;
};
```

### Drawing conventions

- All rendering uses an **off-screen `Pixmap`** (double buffer) to eliminate
  flicker: draw to pixmap, then `XCopyArea` to window on `Expose`.
- Text uses `XftDrawStringUtf8()` with a loaded `XftFont*` (e.g.
  "Sans-10").
- Colors are allocated once per widget type in an `XColor` palette struct via
  `XAllocNamedColor()`.

### Cross-thread event delivery

Xlib has no thread-safe event poster.  To deliver JACK thread notifications
(xrun, server lost, port change) to the X11 event loop:
- Open a POSIX `pipe(pipefd)` at startup.
- The JACK thread (or its idle callbacks) writes a 1-byte event code to
  `pipefd[1]`.
- Register `pipefd[0]` with the `XConnectionNumber(dpy)` in a `select()` /
  `poll()` loop inside the main event dispatch function.
- On `pipefd[0]` becoming readable, read the code and dispatch the appropriate
  handler on the UI thread.

---

## 4. Source Layout

```
src/
  main_linux.cpp                 ← XOpenDisplay, event loop, app init
  linux/
    AppSettings.h/.cpp           ← XDG config load/save (json.hpp)
    AudioEngine.h/.cpp           ← JACK client lifecycle and callback
    JackPortEnum.h/.cpp          ← JACK port discovery
    MainWindow.h/.cpp            ← Top-level Window + Table layout
    ControlBar.h/.cpp            ← HBox: power, gain, record, format
    PluginSlot.h/.cpp            ← Frame + header row per slot
    PluginDialog.h/.cpp          ← Modal Dialog: plugin browser
    ParameterPanel.h/.cpp        ← Dynamic controls from PortInfo
    SettingsDialog.h/.cpp        ← Modal Dialog: audio + preset settings
    ui/
      Widget.h/.cpp              ← Widget base class
      Button.h/.cpp
      ToggleButton.h/.cpp
      Label.h/.cpp
      HSlider.h/.cpp
      ComboBox.h/.cpp
      Frame.h/.cpp
      VBox.h / HBox.h            ← layout helpers (header-only)
      Table.h/.cpp
      ScrollView.h/.cpp
      TextEntry.h/.cpp
      Dialog.h/.cpp
      ListView.h/.cpp
      Theme.h/.cpp               ← color palette, font handles
```

Shared (unchanged):
```
src/
  LiveEffectEngine.h/.cpp
  LV2Plugin.hpp
  FileWriter.h/.cpp
  LockFreeQueue.h/.cpp
  AudioBuffer.h  utils.h  json.hpp
  logging_macros.h  lv2_ringbuffer.h
```

---

## 5. Build System Changes

### New preset in `CMakePresets.json`

```json
{
  "name": "linux-xlib",
  "displayName": "Linux (Xlib + JACK)",
  "binaryDir": "${sourceDir}/build-linux-xlib",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Release",
    "OPIQO_TARGET_PLATFORM": "linux-xlib"
  }
}
```

### CMakeLists.txt additions

```cmake
if(OPIQO_TARGET_PLATFORM STREQUAL "linux-xlib")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(X11     REQUIRED x11)
  pkg_check_modules(XFT     REQUIRED xft)
  pkg_check_modules(XRENDER REQUIRED xrender)
  pkg_check_modules(XEXT    REQUIRED xext)
  pkg_check_modules(JACK    REQUIRED jack)
  pkg_check_modules(LILV    REQUIRED lilv-0)
  pkg_check_modules(SNDFILE REQUIRED sndfile)

  file(GLOB UI_SOURCES src/linux/ui/*.cpp)

  add_executable(opiqo
    src/main_linux.cpp
    src/linux/AppSettings.cpp
    src/linux/AudioEngine.cpp
    src/linux/JackPortEnum.cpp
    src/linux/MainWindow.cpp
    src/linux/ControlBar.cpp
    src/linux/PluginSlot.cpp
    src/linux/PluginDialog.cpp
    src/linux/ParameterPanel.cpp
    src/linux/SettingsDialog.cpp
    src/LiveEffectEngine.cpp
    src/FileWriter.cpp
    src/LockFreeQueue.cpp
    ${UI_SOURCES}
  )

  target_include_directories(opiqo PRIVATE
    ${X11_INCLUDE_DIRS} ${XFT_INCLUDE_DIRS} ${XRENDER_INCLUDE_DIRS}
    ${XEXT_INCLUDE_DIRS} ${JACK_INCLUDE_DIRS} ${LILV_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS} src/ src/linux/ src/linux/ui/
  )

  target_link_libraries(opiqo PRIVATE
    ${X11_LIBRARIES} ${XFT_LIBRARIES} ${XRENDER_LIBRARIES}
    ${XEXT_LIBRARIES} ${JACK_LIBRARIES} ${LILV_LIBRARIES}
    ${SNDFILE_LIBRARIES} pthread dl
  )
endif()
```

### Build commands

```bash
cmake --preset linux-xlib
cmake --build build-linux-xlib -j$(nproc)
```

---

## 6. Architecture Mapping

```
┌──────────────────────────────────────┐
│          Xlib UI Layer               │
│  MainWindow, PluginSlot,             │
│  ControlBar, PluginDialog,           │
│  ParameterPanel, SettingsDialog      │
│  ──────────────────────────────────  │
│  Custom widget toolkit               │
│  (Button, Slider, ComboBox, …)       │
└──────────────────┬───────────────────┘
                   │  calls
┌──────────────────▼───────────────────┐
│       Audio Platform Layer           │
│  AudioEngine (JACK client),          │
│  JackPortEnum                        │
└──────────────────┬───────────────────┘
                   │  calls
┌──────────────────▼───────────────────┐
│          DSP Host Layer              │
│  LiveEffectEngine + LV2Plugin        │
└──────────────────────────────────────┘
```

**Thread ownership:**
- X11 event thread: main thread runs `XNextEvent` loop, dispatches to widgets.
- JACK real-time thread: `jack_process_callback` only.
- Cross-thread bridge: anonymous `pipe(2)` — JACK thread writes, `select()`
  in event loop reads and invokes handlers on the UI thread.
- `std::atomic<State>` for `AudioEngine` state visible to both threads.

---

## 7. Milestone Plan

### Milestone 0: Project Skeleton, Widget Foundation, and Build Wiring

**Goals:** Compile a bare Xlib window; implement and test core widget primitives.

**Tasks:**

*Build skeleton:*
- Add `linux-xlib` preset to `CMakePresets.json`.
- Add the platform block to `CMakeLists.txt`.
- Create `src/main_linux.cpp`:
  ```cpp
  #include <X11/Xlib.h>
  int main() {
      XInitThreads();    // must be first Xlib call
      Display* dpy = XOpenDisplay(nullptr);
      Window root = DefaultRootWindow(dpy);
      Window win  = XCreateSimpleWindow(dpy, root,
                        0, 0, 900, 650, 0, 0,
                        BlackPixel(dpy, DefaultScreen(dpy)));
      XStoreName(dpy, win, "Opiqo");
      XMapWindow(dpy, win);
      XEvent ev;
      while (true) {
          XNextEvent(dpy, &ev);
          if (ev.type == DestroyNotify) break;
      }
      XCloseDisplay(dpy);
  }
  ```

*Core widget primitives (Milestone 0 deliverable set):*
- `Theme`: load `XftFont`, allocate color palette (`XAllocNamedColor`), store
  `XftColor` values for background, foreground, accent, border, disabled.
- `Widget` base: `create()`, `show()`, `hide()`, `resize()`, `draw()`,
  `handleEvent()`.
- `Button`: normal/hover/pressed states; `"clicked"` callback via
  `std::function<void()>`.
- `ToggleButton`: latched `Button`; `"toggled"` callback with bool.
- `Label`: `XftDrawStringUtf8` with `setText()`.
- `HSlider`: draggable thumb, `[min, max]` range, `"value-changed"` callback.
- `Frame`: border rectangle + optional `XftDraw` title text.
- `HBox` / `VBox`: pack children with spacing; `relayout()` on resize.
- `Table`: row × col grid; `attach(widget, col, row)`.

**Deliverables:**
- `build-linux-xlib/opiqo` opens a blank titled window.
- Widget unit test binary (`build-linux-xlib/widget_test`) exercises Button,
  Slider, Label rendering and click callbacks via synthetic `XEvent`s.

**Acceptance criteria:**
- Build succeeds with no warnings on a system with `libX11`, `libXft`, JACK ≥ 1.9.
- Blank window opens and closes cleanly; no X11 error handler callbacks fire.

---

### Milestone 1: JACK Port Enumeration and Settings Model

**Goals:** Discover available JACK client ports and persist user preferences.

**Tasks:**

*`JackPortEnum`*:
- Open a temporary JACK client (`jack_client_open("opiqo-enum", JackNoStartServer, nullptr)`).
- `enumerateCapturePorts()` / `enumeratePlaybackPorts()` via `jack_get_ports()`
  with `JackPortIsPhysical`.
- Return `PortInfo { std::string id; std::string friendlyName; bool isDefault; }`.
- Register `jack_set_port_registration_callback()` to write a `'P'` byte to
  `pipefd[1]` (the notification pipe); the UI thread reads it and refreshes the
  port dropdowns.
- `static std::string resolveOrDefault(list, saved)`.

*`AppSettings`*:
- Fields: `capturePort`, `playbackPort`, `sampleRate` (JACK read-only),
  `recordFormat`, `recordQuality`, `gain`.
- Load/save `~/.config/opiqo/settings.json` via `json.hpp`.

**Deliverables:** Port list populated; settings persist across restarts.

**Acceptance criteria:**
- Port list refreshes when a USB audio device is hotplugged.
- Missing saved port falls back to first physical capture/playback port.

---

### Milestone 2: Remaining Widget Primitives

**Goals:** Complete the widget set needed by the application UI.

**Tasks:**
- `ScrollView`: clip child to viewport, vertical scrollbar track + thumb,
  handle `ButtonPress` on scrollbar and `MotionNotify` while dragging.
- `ComboBox`: shows current value label + arrow button; on click opens a
  transient `Window` with a `ListView` of items; selection closes the popup
  and fires `"changed"` callback with item index.
- `TextEntry`: single-line editable field; `KeyPress` → `XLookupKeysym()` →
  insert/delete character, `BackSpace`, `Delete`, caret blink via
  `XtAppAddTimeOut`-equivalent (`g`-less: use `select()` with timeout in the
  event loop).
- `ListView`: scrollable list of string items; click to select, double-click
  to confirm (`"row-activated"` callback); keyboard Up/Down navigation.
- `Dialog`: `XCreateWindow` with `override_redirect = False`,
  `WM_TRANSIENT_FOR` hint set to main window, grab pointer with
  `XGrabPointer()` for modality; dismiss on response button click or
  `WM_DELETE_WINDOW`.

**Deliverables:** All widget types exercised in the widget test binary.

**Acceptance criteria:**
- ComboBox popup appears, item selection works, popup closes correctly.
- ListView scrolls to 100+ items without redraw artefacts.
- Dialog modality prevents click-through to parent window.

---

### Milestone 3: AudioEngine Core (JACK, Pass-Through)

**Goals:** Prove full-duplex JACK pipeline with a clean pass-through path.

**Tasks:**

*`AudioEngine` public API:*
```cpp
enum class State { Off, Starting, Running, Stopping, Error };

bool start(const std::string& capturePort,
           const std::string& playbackPort);
void stop();
State       state()        const;
int32_t     sampleRate()   const;
int32_t     blockSize()    const;
std::string errorMessage() const;
uint64_t    xrunCount()    const;
```

*Internals:*
- `jack_client_open("opiqo", JackNoStartServer, nullptr)`.
- Register one capture port and one playback port via `jack_port_register()`.
- `jack_process_callback`: memcpy input → output (pass-through), no DSP yet.
- `jack_set_xrun_callback`: increment `xrunCount_` atomically; write `'X'` to
  the notification pipe.
- `jack_set_server_lost_callback`: set state to `Error`; write `'E'` to the
  notification pipe.
- `start()` → `jack_activate()` → `jack_connect()` to selected physical ports.
- `stop()` → `jack_deactivate()` → `jack_client_close()`.

**Deliverables:** Stable JACK pass-through stream.

**Acceptance criteria:**
- 15-minute pass-through run without xruns.
- 50 start/stop cycles without leaked JACK clients.

---

### Milestone 4: LiveEffectEngine Integration in JACK Callback

**Goals:** Replace pass-through with the real DSP path.

**Tasks:**
- In `jack_process_callback`, call
  `engine_->process(capture_buf, playback_buf, nframes)`.
- JACK buffers are `float` — no conversion needed.
- Register a second stereo port pair; remix mono→stereo if plugin needs it.
- Preallocate interleave/deinterleave scratch buffers in `AudioEngine::start()`.
- Validate bypass: with no plugins loaded, signal passes through cleanly.

**Deliverables:** Real-time plugin processing over JACK.

**Acceptance criteria:**
- With no plugins loaded: clean pass-through audible.
- With a plugin in slot 1: effect audible on output.
- No allocations in the JACK callback (`MALLOC_CHECK_=3`).

---

### Milestone 5: Power Toggle State Machine

**Goals:** Make the Power button the single source of truth for engine transport.

**Tasks:**
- `std::atomic<State>` transitions:
  `Off → Starting → Running`, `Running → Stopping → Off`, any → `Error`.
- Wire `ControlBar` power `ToggleButton`:
  - toggled On → `AudioEngine::start()`.
  - toggled Off → `AudioEngine::stop()`.
- Notification pipe reader in the event loop checks `'E'` (Error) and `'X'`
  (xrun) codes; Error reverts the power toggle and updates the status `Label`.
- Xrun counter label in the status bar updated on every `'X'` code.

**Deliverables:** Reliable transport control.

**Acceptance criteria:**
- 50× rapid toggle spam leaves engine in a consistent state.
- JACK server crash is surfaced in the UI within one event-loop iteration.

---

### Milestone 6: MainWindow Layout and 2×2 Plugin Slot Grid

**Goals:** Build the full Xlib shell layout.

**Tasks:**

*Widget tree:*
```
Window "opiqo"  (900 × 650 minimum)
  VBox
    HBox "menuRow"            ← File | Settings text buttons (Label-style)
    Table (2 cols × 2 rows)   ← plugin slots, expand to fill
      PluginSlot[0]  col=0 row=0
      PluginSlot[1]  col=1 row=0
      PluginSlot[2]  col=0 row=1
      PluginSlot[3]  col=1 row=1
    ControlBar (HBox)         ← power toggle, gain slider, record toggle,
                                 format ComboBox, quality ComboBox
    Label "statusBar_"        ← bottom status / xrun counter
```

- Handle `ConfigureNotify` on the top-level window to trigger `relayout()`.
- `WM_DELETE_WINDOW` atom registered so closing the window exits cleanly.
- Minimum window size enforced with `XSetWMNormalHints` (`PMinSize` flag).

**Deliverables:** Full shell layout with placeholder slot frames.

**Acceptance criteria:**
- Window resizes; `Table` slots scale with equal proportions.
- No `BadDrawable` X11 errors during rapid resize.

---

### Milestone 7: Plugin Management UI

**Goals:** Connect plugin lifecycle controls to existing `LiveEffectEngine` APIs.

**Tasks:**

*`PluginSlot` (one `Frame` per slot):*
```
Frame (titled with slot index)
  VBox
    HBox                       ← header row
      Label  pluginName_
      Button "+"  addButton_
      ToggleButton "Bypass"  bypassButton_
      Button "×"  deleteButton_
    ParameterPanel             ← ScrollView; filled after add
```

*`PluginDialog` (`Dialog`):*
- `TextEntry` for search filter at the top.
- `ListView` populated from `LiveEffectEngine::getAvailablePlugins()`.
- Filter: on `TextEntry` `"changed"`, rebuild the `ListView` items to those
  whose name contains the query string (case-insensitive `tolower` compare).
- "Add" `Button` and "Cancel" `Button` at the bottom.
- On "Add": call `engine_->addPlugin(slot, uri)`, close dialog, rebuild panel.

*Per-slot controls:*
- **Add (+)**: opens `PluginDialog`; on confirm, `addPlugin` + `buildParameterPanel`.
- **Bypass**: `engine_->setPluginEnabled(slot, !bypassed)`.
- **Delete (×)**: `engine_->deletePlugin(slot)` + `clearParameterPanel`.

**Deliverables:** End-to-end plugin add/delete/enable from UI.

**Acceptance criteria:**
- Each of the 4 slots independently manages its plugin instance.
- Bypass toggles effect in real time with no audible glitch.

---

### Milestone 8: Dynamic Parameter Panels

**Goals:** Generate Xlib controls at runtime from `LV2Plugin::PortInfo`.

**Tasks:**

*Control type → widget mapping:*

| `PortInfo::ControlType` | Widget |
|-------------------------|--------|
| `Float` (range)         | `HSlider` (range mapped to `[min, max]`) |
| `Float` (enumeration)   | `ComboBox` with scale-point labels |
| `Toggle`                | `ToggleButton` (checkbox style) |
| `Trigger`               | `Button` (fires `engine_->setValue(slot, idx, 1.0f)` on `"clicked"`) |
| `AtomFilePath`          | `Button` "Browse…" → custom file-chooser `Dialog` backed by `opendir`/`readdir` |

- `ParameterPanel` is a `ScrollView` containing a `VBox`.
- Build loop: `engine_->getPluginPortInfo(slot)` → for each port, add an `HBox`
  row: `Label` (port name) + control widget.
- `HSlider` `"value-changed"` callback → `engine_->setValue()`.
- `ComboBox` `"changed"` callback → map index to scale-point value →
  `engine_->setValue()`.
- `ToggleButton` `"toggled"` callback → `engine_->setValue(slot, idx, on?1:0)`.
- Clear panel: call `destroy()` on each child widget and remove from `VBox`.

**Deliverables:** Fully dynamic parameter UI for loaded plugins.

**Acceptance criteria:**
- Slider drag updates sound in real time without JACK xruns.
- Panels scroll cleanly with 50+ ports.
- Panel rebuild after plugin change leaves no orphaned X windows.

---

### Milestone 9: Recording UX and Format Handling

**Goals:** Wire recording controls to `FileWriter` via `LiveEffectEngine`.

**Tasks:**
- `ControlBar` record `ToggleButton`: on →
  `engine_->startRecording(fd, format, quality)`; off →
  `engine_->stopRecording()`.
- Output path: `~/.local/share/opiqo/YYYY-MM-DD_HH-MM-SS.{wav,mp3,ogg}`;
  create directory with `mkdir -p` equivalent (`::mkdir` with mode 0755,
  check `EEXIST`).
- Format `ComboBox` items: WAV / MP3 / OGG; `"changed"` fires
  `show()`/`hide()` on the quality `ComboBox`.
- File-chooser `Dialog` (AtomFilePath): custom `Dialog` listing the current
  directory via `opendir()`/`readdir()`, `ListView` for directory entries,
  `TextEntry` for filename, navigation buttons (`Parent Dir`, `Home`).

**Deliverables:** Complete recording workflow.

**Acceptance criteria:**
- Recorded WAV/MP3/OGG files open in external players without corruption.
- Recording runs concurrently with all 4 plugin slots active.
- File is finalized cleanly even if the window is closed during recording.

---

### Milestone 10: Settings Dialog and Runtime Reconfiguration

**Goals:** Configurable audio preferences and preset import/export.

**Tasks:**

*`SettingsDialog` (`Dialog`, two tab bar rows via `HBox` buttons switching a
`VBox` content area — a manual notebook):*

**Audio tab:**
- JACK capture port `ComboBox` populated by `JackPortEnum`.
- JACK playback port `ComboBox`.
- `Label` for sample rate and buffer size (JACK server values).
- "Apply" `Button`: stop engine if running → apply → restart.

**Presets tab:**
- Export `Button`: file-chooser `Dialog` (save mode) → write
  `engine_->getPresetList()` JSON to file.
- Import `Button`: file-chooser `Dialog` (open mode) → read JSON →
  call `engine_->applyPreset()` per slot.

**Deliverables:** Full settings workflow.

**Acceptance criteria:**
- Port selection survives app restart.
- Preset import restores all four slot configurations.
- Settings apply without app restart.

---

### Milestone 11: Hardening, Performance, and Release Readiness

**Goals:** Production stability and diagnosability.

**Tasks:**
- Set JACK thread to `SCHED_FIFO` via `JackRealTime` option in
  `jack_client_open` or `pthread_setschedparam` post-activation.
- Verify `XInitThreads()` is the very first Xlib call in `main()`.
- Add a rate-limited xrun counter `Label` in the status bar, updated via the
  notification pipe mechanism.
- Log xruns and errors to `~/.local/share/opiqo/opiqo.log` via
  `logging_macros.h`.
- Stress test: resize window rapidly 1000× while audio is running; confirm no
  `BadDrawable` errors and no JACK xruns from UI thread contention.
- Verify JACK callback is allocation-free with `MALLOC_CHECK_=3`.
- Confirm PipeWire-JACK compatibility via `pipewire-jack` shim.

**Deliverables:** Release candidate build with validation report.

**Acceptance criteria:**
- 60-minute soak without xruns on a quiet system.
- JACK server restart recovers engine without app restart.
- Zero X11 error callbacks during a full UI interaction session.

---

## 8. Cross-Cutting Technical Requirements

### Xlib thread safety
- Call `XInitThreads()` as the very first X11 call in `main()`.
- Only the UI (main) thread calls any Xlib function.
- JACK thread communicates via the anonymous `pipe(2)` only — never calls
  Xlib directly.

### Real-time safety (JACK callback)
- No heap allocations inside `jack_process_callback`.
- No mutexes that can block; use `LockFreeQueue` for parameter updates.
- No Xlib calls; post notifications via `write(pipefd[1], &code, 1)`.
- Preallocate all scratch buffers in `AudioEngine::start()`.

### Event loop design

```cpp
// Main event loop skeleton (main_linux.cpp)
int xfd      = XConnectionNumber(dpy);   // X11 socket fd
int pipeRfd  = pipefd[0];               // JACK notification read end

while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(xfd, &fds);
    FD_SET(pipeRfd, &fds);
    int maxfd = std::max(xfd, pipeRfd) + 1;
    select(maxfd, &fds, nullptr, nullptr, nullptr);

    // Drain all pending X events
    while (XPending(dpy)) {
        XNextEvent(dpy, &ev);
        rootWidget.handleEvent(ev);
    }

    // Drain notification pipe
    if (FD_ISSET(pipeRfd, &fds)) {
        char code;
        read(pipeRfd, &code, 1);
        onJackNotification(code);  // runs on UI thread
    }
}
```

### Error handling
- Install `XSetErrorHandler` and `XSetIOErrorHandler` at startup to log and
  recover from non-fatal X11 errors.
- `AudioEngine` state transitions to `Error` on JACK server loss.
- `AppSettings::load()` returns safe defaults on any file error.

---

## 9. File Group Summary

| File group | New files |
|---|---|
| App entry | `src/main_linux.cpp` |
| Widget toolkit | `src/linux/ui/Widget.h/.cpp`, `Button`, `ToggleButton`, `Label`, `HSlider`, `ComboBox`, `Frame`, `VBox.h`, `HBox.h`, `Table`, `ScrollView`, `TextEntry`, `Dialog`, `ListView`, `Theme` |
| Audio platform | `src/linux/AudioEngine.h/.cpp`, `src/linux/JackPortEnum.h/.cpp` |
| Settings | `src/linux/AppSettings.h/.cpp` |
| UI shell | `src/linux/MainWindow.h/.cpp`, `src/linux/ControlBar.h/.cpp` |
| Plugin UX | `src/linux/PluginSlot.h/.cpp`, `src/linux/PluginDialog.h/.cpp`, `src/linux/ParameterPanel.h/.cpp` |
| Settings dialog | `src/linux/SettingsDialog.h/.cpp` |
| Build | `CMakePresets.json` (`linux-xlib` preset), `CMakeLists.txt` (new target block) |

---

## 10. Test Plan Summary

### Functional tests
- App startup with valid and missing saved JACK ports.
- Power toggle on/off cycles (50 repetitions).
- JACK port switch while running.
- Add/delete plugins in all 4 slots.
- Parameter changes under active audio.
- Recording for WAV, MP3, and OGG while all slots are active.
- File-chooser dialog: navigate directories, select file, cancel.

### Robustness tests
- USB audio device unplug/replug while JACK graph is active.
- Rapid Power toggle spam (100×).
- JACK server kill and restart while engine is Running.
- Rapid window resize while audio is running (1000 resize events).
- X11 display disconnect (`DISPLAY` unset mid-run) — confirm clean exit.

### Performance tests
- CPU usage (UI thread): idle window, with 4 active slots.
- Xrun count over 10 minutes at 48 kHz / 256-frame buffer.
- Expose event handling time: confirm no redraws exceed 8 ms.

---

## 11. Prerequisites

Install the following packages before building:

### Debian / Ubuntu
```bash
sudo apt install \
  libx11-dev \
  libxft-dev \
  libxrender-dev \
  libxext-dev \
  libjack-jackd2-dev \
  liblilv-dev \
  libsndfile1-dev \
  libmp3lame-dev \
  libopus-dev \
  libopusenc-dev \
  cmake make pkg-config
```

### Fedora
```bash
sudo dnf install \
  libX11-devel \
  libXft-devel \
  libXrender-devel \
  libXext-devel \
  jack-audio-connection-kit-devel \
  lilv-devel \
  libsndfile-devel \
  lame-devel \
  opus-devel \
  opusenc-devel \
  cmake make pkg-config
```

---

## 12. Sequencing Recommendation

1. **Milestones 0–2** — Widget foundation and JACK port enumeration.
2. **Milestones 3–5** — Audio pipeline (pass-through → DSP → power toggle).
3. **Milestones 6–8** — Shell layout → plugin management → parameter panels.
4. **Milestones 9–10** — Recording → settings dialog.
5. **Milestone 11** — Hardening and release.

Build the widget toolkit first (Milestone 0 + 2) before investing in
application-level UI work, since every subsequent milestone depends on it.
