# Opiqo Linux Host Implementation Plan

## 1. Objective

Build a native Linux port of the Opiqo LV2 plugin host that:
- runs low-latency duplex audio via the JACK Audio Connection Kit,
- processes audio through the existing `LiveEffectEngine` in real time,
- provides a GTK3 desktop UI with 4 plugin slots in a 2×2 layout,
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
| UI toolkit | Win32 API | GTK3 |
| Audio backend | WASAPI | JACK (libjack2) |
| Device discovery | IMMDeviceEnumerator (COM) | `jack_get_ports()` |
| Hot-plug events | IMMNotificationClient | `jack_set_port_registration_callback()` |
| Settings storage | `%APPDATA%\Opiqo\settings.json` | `~/.config/opiqo/settings.json` (XDG) |
| Build | MinGW cross-compile | Native CMake, pkg-config |
| UI definition | Win32 resources (.rc) | GtkBuilder .ui XML |

### Why JACK instead of PipeWire-native or ALSA?

- JACK provides a guaranteed real-time callback (`jack_process_callback`) with
  float32 buffers matching `LiveEffectEngine`'s expected format — zero
  conversion required.
- PipeWire ships a full JACK compatibility layer (`pipewire-jack`); the same
  binary works on both classic JACK2 and modern PipeWire systems.
- ALSA would require manual latency management and ring-buffer design already
  solved by JACK.

---

## 3. Source Layout

New files are placed in `src/linux/` to mirror `src/win32/`.

```
src/
  main_linux.cpp              ← GtkApplication entry point
  linux/
    AppSettings.h/.cpp        ← XDG config load/save
    AudioEngine.h/.cpp        ← JACK client lifecycle and callback
    JackPortEnum.h/.cpp       ← JACK port discovery (replaces WasapiDeviceEnum)
    MainWindow.h/.cpp         ← GtkApplicationWindow + layout
    ControlBar.h/.cpp         ← GtkBox: power, gain, record, format
    PluginSlot.h/.cpp         ← GtkFrame per slot
    PluginDialog.h/.cpp       ← GtkDialog: plugin browser
    ParameterPanel.h/.cpp     ← Dynamic GTK3 controls from PortInfo
    SettingsDialog.h/.cpp     ← GtkDialog: audio + preset settings
```

Shared (unchanged):
```
src/
  LiveEffectEngine.h/.cpp
  LV2Plugin.hpp
  FileWriter.h/.cpp
  LockFreeQueue.h/.cpp
  AudioBuffer.h
  utils.h
  json.hpp
  logging_macros.h
  lv2_ringbuffer.h
```

---

## 4. Build System Changes

Add a new CMake preset and target alongside the existing Windows preset.

### New preset in `CMakePresets.json`

```json
{
  "name": "linux-default",
  "displayName": "Linux (GTK3 + JACK)",
  "binaryDir": "${sourceDir}/build-linux",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Release",
    "OPIQO_TARGET_PLATFORM": "linux"
  }
}
```

### CMakeLists.txt additions

```cmake
if(OPIQO_TARGET_PLATFORM STREQUAL "linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK3   REQUIRED gtk+-3.0)
  pkg_check_modules(JACK   REQUIRED jack)
  pkg_check_modules(LILV   REQUIRED lilv-0)
  pkg_check_modules(SNDFILE REQUIRED sndfile)

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
  )

  target_include_directories(opiqo PRIVATE
    ${GTK3_INCLUDE_DIRS} ${JACK_INCLUDE_DIRS} ${LILV_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS} src/ src/linux/
  )

  target_link_libraries(opiqo PRIVATE
    ${GTK3_LIBRARIES} ${JACK_LIBRARIES} ${LILV_LIBRARIES}
    ${SNDFILE_LIBRARIES} pthread dl
  )
endif()
```

### Build commands

```bash
cmake --preset linux-default
cmake --build build-linux -j$(nproc)
```

---

## 5. Architecture Mapping

```
┌─────────────────────────────────┐
│         GTK3 UI Layer           │
│  MainWindow, PluginSlot,        │
│  ControlBar, PluginDialog,      │
│  ParameterPanel, SettingsDialog │
└────────────────┬────────────────┘
                 │  calls
┌────────────────▼────────────────┐
│      Audio Platform Layer       │
│  AudioEngine (JACK client),     │
│  JackPortEnum                   │
└────────────────┬────────────────┘
                 │  calls
┌────────────────▼────────────────┐
│        DSP Host Layer           │
│  LiveEffectEngine + LV2Plugin   │
└─────────────────────────────────┘
```

**Thread ownership:**
- GTK main thread handles all widget events and UI state.
- JACK real-time callback thread runs `LiveEffectEngine::process()`.
- Cross-thread communication uses `LockFreeQueue` (already in the codebase) and
  `g_idle_add()` to marshal JACK events back to the GTK main thread.

---

## 6. Milestone Plan

### Milestone 0: Project Skeleton and Build Wiring

**Goals:** Compile a minimal GTK3 window under the new `linux-default` preset.

**Tasks:**
- Add the `linux-default` preset to `CMakePresets.json`.
- Add the `if(OPIQO_TARGET_PLATFORM STREQUAL "linux")` block to `CMakeLists.txt`.
- Create `src/main_linux.cpp` with a `GtkApplication` that opens a blank
  `GtkApplicationWindow` (requires GTK3 ≥ 3.4 for `GtkApplication`).
- Add stub header/source files for all `src/linux/` modules.

**Deliverables:** `build-linux/opiqo` binary that opens an empty GTK3 window.

**Acceptance criteria:**
- `cmake --preset linux-default && cmake --build build-linux` succeeds without
  warnings on a system with GTK3 ≥ 3.22 and JACK ≥ 1.9.
- Binary runs and shows a window without crashes.

---

### Milestone 1: JACK Port Enumeration and Settings Model

**Goals:** Discover available JACK client ports and persist user preferences.

**Tasks:**

*`JackPortEnum`*:
- Open a temporary JACK client (`jack_client_open("opiqo-enum", …)`) for probing.
- Implement `enumerateCapturePorts()` and `enumeratePlaybackPorts()` using
  `jack_get_ports()` with `JackPortIsPhysical` flag.
- Return a `PortInfo { id, friendlyName, isDefault }` list matching the shared
  `DeviceInfo` interface used by the rest of the engine.
- Register `jack_set_port_registration_callback()` to fire a
  `std::function<void()>` change callback; marshal it to the GTK main thread via
  `g_idle_add()`.

*`AppSettings`*:
- Fields: `capturePort`, `playbackPort`, `sampleRate` (read-only from JACK
  server), `recordFormat`, `recordQuality`, `gain`.
- Load from `$XDG_CONFIG_HOME/opiqo/settings.json` (falls back to
  `~/.config/opiqo/settings.json`).
- Save on app shutdown.

**Deliverables:** `JackPortEnum` returns a populated port list; settings round-trip.

**Acceptance criteria:**
- Port list refreshes when a device is plugged/unplugged.
- Missing saved port falls back to first physical capture/playback port.

---

### Milestone 2: AudioEngine Core (JACK, Pass-Through)

**Goals:** Prove full-duplex JACK pipeline with a clean pass-through path.

**Tasks:**

*`AudioEngine` public API* (same shape as Windows counterpart):
```cpp
enum class State { Off, Starting, Running, Stopping, Error };

bool start(const std::string& capturePort,
           const std::string& playbackPort);
void stop();
State       state()        const;
int32_t     sampleRate()   const;
int32_t     blockSize()    const;   // jack_get_buffer_size()
std::string errorMessage() const;
uint64_t    xrunCount()    const;   // replaces dropoutCount
```

*Internals:*
- `jack_client_open("opiqo", JackNoStartServer, nullptr)`.
- Register one capture port (`jack_port_register`) and one playback port.
- Implement `jack_process_callback`: copy input buffer to output buffer
  (pass-through) without calling `LiveEffectEngine` yet.
- Register `jack_set_xrun_callback` to increment `xrunCount_`.
- `start()` calls `jack_activate()` then `jack_connect()` to the selected
  physical ports.
- `stop()` calls `jack_deactivate()` then `jack_client_close()`.
- Expose `sampleRate()` via `jack_get_sample_rate()` and `blockSize()` via
  `jack_get_buffer_size()`.

**Deliverables:** Stable pass-through from JACK capture to playback port.

**Acceptance criteria:**
- 15-minute pass-through run without xruns on a quiet system.
- 50 repeated `start()`/`stop()` cycles without leaked JACK clients.

---

### Milestone 3: LiveEffectEngine Integration in JACK Callback

**Goals:** Replace pass-through with the real DSP path.

**Tasks:**
- In `jack_process_callback`, call
  `engine_->process(capture_buf, playback_buf, nframes)`.
- `LiveEffectEngine` already works with float32 buffers — no format conversion
  needed (JACK native format is `jack_default_audio_sample_t` = `float`).
- Verify stereo channel handling: register a second capture and playback port
  pair, or remix mono input to both channels if the loaded plugin expects stereo.
- Validate bypass: with no plugins loaded, signal passes through cleanly.

**Deliverables:** Real-time plugin processing over JACK.

**Acceptance criteria:**
- With no plugins loaded: clean pass-through audible.
- With a plugin loaded in slot 1: effect audible on output.
- No allocations in the JACK callback (verified with
  `MALLOC_CHECK_=3` environment variable during testing).

---

### Milestone 4: Power Toggle State Machine

**Goals:** Make the Power button the single source of truth for engine transport.

**Tasks:**
- Implement atomic `State` variable with transitions:
  `Off → Starting → Running`, `Running → Stopping → Off`,
  any state → `Error` on JACK failure.
- Wire `ControlBar` power toggle:
  - toggled On → `AudioEngine::start()` with selected ports.
  - toggled Off → `AudioEngine::stop()`.
- Use `g_timeout_add(200, pollEngineState, this)` to poll `AudioEngine::state()`
  on the GTK main thread and update the toggle and status bar.
- On `State::Error`: revert toggle to Off, show error message in status bar.
- JACK server-lost callback (`jack_set_server_lost_callback`) transitions engine
  to `Error` and signals the main thread via `g_idle_add`.

**Deliverables:** Reliable and deterministic transport behavior.

**Acceptance criteria:**
- Rapid start/stop spam (50×) leaves engine in a consistent state.
- JACK server crash moves UI to Error state without hang.

---

### Milestone 5: MainWindow Layout and 2×2 Plugin Slot Grid

**Goals:** Build the full GTK3 shell layout.

**Tasks:**

*GTK3 widget hierarchy:*
```
GtkApplicationWindow "opiqo"
  GtkBox (vertical, spacing=0)
    GtkHeaderBar  ← title + menu button
    GtkGrid (2 cols × 2 rows, expand=TRUE)  ← plugin slots
      PluginSlot[0]  col=0 row=0
      PluginSlot[1]  col=1 row=0
      PluginSlot[2]  col=0 row=1
      PluginSlot[3]  col=1 row=1
    ControlBar (GtkBox, horizontal)  ← power, gain, record, format
    GtkStatusbar "statusBar_"  ← bottom status text
```

- Define the static skeleton in a `main_window.ui` GtkBuilder file.
- `MainWindow::create()` calls
  `gtk_builder_new_from_file()` or `gtk_builder_new_from_resource()` with a
  compiled `GResource` bundle.
- Minimum window size: 900 × 650 (set via `gtk_window_set_default_size`).
- CSS file `opiqo.css` loaded via `GtkCssProvider` and applied with
  `gtk_style_context_add_provider_for_screen()` for basic slot framing.

**Deliverables:** Full shell layout with placeholder slots.

**Acceptance criteria:**
- Window resizes gracefully; slots scale proportionally.
- Layout renders correctly on both X11 and Wayland.

---

### Milestone 6: Plugin Management UI

**Goals:** Connect plugin lifecycle controls to existing `LiveEffectEngine` APIs.

**Tasks:**

*`PluginSlot` (one `GtkFrame` per slot):*
```
GtkFrame
  GtkBox (vertical)
    GtkBox (horizontal)   ← header row
      GtkLabel  pluginName_
      GtkButton "+"        addButton_
      GtkToggleButton "Bypass"  bypassButton_
      GtkButton "×"        deleteButton_
    PluginParameterPanel   ← filled after add
```

*`PluginDialog` (`GtkDialog`):*
- Backed by `LiveEffectEngine::getAvailablePlugins()`.
- Display with `GtkTreeView` + `GtkListStore` (columns: plugin name, URI).
- Filter entry using `GtkSearchEntry` bound to a `GtkTreeModelFilter` that
  filters on the plugin name column.
- Confirm button calls `engine_->addPlugin(slot, uri)` and rebuilds
  `ParameterPanel`.

*Per-slot controls:*
- **Add (+)**: opens `PluginDialog`; on confirm, calls `addPlugin` and
  `buildParameterPanel`.
- **Bypass**: calls `engine_->setPluginEnabled(slot, !bypassed)`.
- **Delete (×)**: calls `engine_->deletePlugin(slot)` and calls
  `clearParameterPanel`.

**Deliverables:** End-to-end plugin add/delete/enable from UI.

**Acceptance criteria:**
- Each of the 4 slots independently manages its plugin instance.
- Bypass toggles effect in real time with no audible glitch.

---

### Milestone 7: Dynamic Parameter Panels

**Goals:** Generate GTK3 controls at runtime from `LV2Plugin::PortInfo`.

**Tasks:**

*Control type → GTK3 widget mapping:*

| `PortInfo::ControlType` | GTK3 widget |
|-------------------------|-------------|
| `Float` (range)         | `GtkScale` (horizontal, `GTK_ORIENTATION_HORIZONTAL`) |
| `Float` (enumeration)   | `GtkComboBoxText` with scale-point labels appended via `gtk_combo_box_text_append_text()` |
| `Toggle`                | `GtkCheckButton` |
| `Trigger`               | `GtkButton` (fires `engine_->setValue(slot, idx, 1.0f)`) |
| `AtomFilePath`          | `GtkButton` "Browse…" → `GtkFileChooserDialog` (action `GTK_FILE_CHOOSER_ACTION_OPEN`) |

- `ParameterPanel` is a `GtkScrolledWindow` containing a `GtkBox` (vertical,
  `GTK_ORIENTATION_VERTICAL`).
- Build loop calls `engine_->getPluginPortInfo(slot)` and appends a row
  (`GtkBox` horizontal: `GtkLabel` + control widget) per port.
- Value changes connect to `engine_->setValue()` via `g_signal_connect`
  (`value-changed` for `GtkScale`, `changed` for `GtkComboBoxText`,
  `toggled` for `GtkCheckButton`).
- All callbacks fire on the GTK main thread — no special marshalling needed.

**Deliverables:** Fully dynamic parameter UI for loaded plugins.

**Acceptance criteria:**
- Slider movements update sound in real time without JACK xruns.
- Panels scroll cleanly when a plugin exposes many ports.
- Rebuilding the panel on plugin change leaves no dangling widget references.

---

### Milestone 8: Recording UX and Format Handling

**Goals:** Wire recording controls to `FileWriter` via `LiveEffectEngine`.

**Tasks:**
- `ControlBar` record toggle: on → `engine_->startRecording(fd, format, quality)`;
  off → `engine_->stopRecording()`.
- Output file path generated with `g_get_user_special_dir(G_USER_DIRECTORY_MUSIC)`
  + timestamp filename.
- Format dropdown bound to `AppSettings::recordFormat` (0=WAV, 1=MP3, 2=OGG).
- Quality dropdown shown only for lossy formats (MP3, OGG); hidden for WAV.
- Recording status indicated by toggling record button label ("Record" / "Stop").

**Deliverables:** Complete recording workflow during live JACK processing.

**Acceptance criteria:**
- Recorded WAV/MP3/OGG files open in external players without corruption.
- Recording can start and stop while all 4 plugin slots are active.
- File is finalized cleanly even if the app exits during recording.

---

### Milestone 9: Settings Dialog and Runtime Reconfiguration

**Goals:** Configurable audio preferences and preset import/export.

**Tasks:**

*`SettingsDialog` (`GtkDialog`, two tabs via `GtkNotebook`):*

**Audio tab:**
- JACK capture port dropdown (populated by `JackPortEnum`).
- JACK playback port dropdown.
- Read-only sample rate and buffer size labels (provided by JACK server).
- "Apply" button: if engine is running, stop → apply settings → restart.

**Presets tab:**
- Export button: `GtkFileChooserDialog` (action `GTK_FILE_CHOOSER_ACTION_SAVE`)
  → writes result of `engine_->getPresetList()` to JSON.
- Import button: `GtkFileChooserDialog` (action `GTK_FILE_CHOOSER_ACTION_OPEN`)
  → reads JSON and calls `engine_->applyPreset()` per slot.

*Persistence:*
- Settings applied on dialog confirm are saved via `AppSettings::save()`.

**Deliverables:** Full settings workflow with persistent preferences.

**Acceptance criteria:**
- Port selection changes survive app restart.
- Import of a preset restores all four slot configurations.
- Changing the port while the engine is running restarts seamlessly.

---

### Milestone 10: Hardening, Performance, and Release Readiness

**Goals:** Production stability and diagnosability.

**Tasks:**
- Set JACK thread to `SCHED_FIFO` real-time priority by requesting it via
  `jack_client_open` flags or `pthread_setschedparam` after activation.
- Add xrun counter displayed in the status bar, updating via the GTK 200 ms poll.
- Log xruns and engine errors to `~/.local/share/opiqo/opiqo.log` using the
  existing `logging_macros.h` macros.
- Add device hot-plug stress test: plug/unplug a USB audio device 20× while
  running and confirm reconnect without app restart.
- Verify JACK callback is allocation-free with Valgrind `--tool=massif` and
  address sanitizer.
- Confirm PipeWire-JACK compatibility: run against `pipewire-jack` shim and
  verify identical behaviour to JACK2.

**Deliverables:** Release candidate build with validation report.

**Acceptance criteria:**
- 60-minute soak run without xruns on a quiet system.
- JACK server restart recovers engine without app restart.
- Zero memory leaks reported by Valgrind on clean startup/shutdown.

---

## 7. Cross-Cutting Technical Requirements

### Real-time safety (JACK callback)
- No heap allocations inside `jack_process_callback`.
- No mutexes that can block; use `LockFreeQueue` (already in codebase) for
  plugin parameter updates.
- No GTK calls; communicate back to UI via `g_idle_add()`.
- Preallocate all conversion and channel-split buffers in `AudioEngine::start()`.

### Thread safety
- GTK main thread: all widget creation, signal handlers, and UI state changes.
- JACK real-time thread: `jack_process_callback` only.
- `std::atomic<State>` for engine state visible to both threads.

### Error handling
- Surface all JACK errors in the status bar via `g_idle_add`.
- On `jack_set_server_lost_callback`: move to `State::Error`, show message,
  revert Power toggle.
- `AppSettings::load()` returns safe defaults on any file error.

### Wayland / X11 compatibility
- Use `GtkFileChooserDialog` for all file choosers; it works on both X11 and
  Wayland under GTK3's built-in backends.
- Avoid `GdkX11Window` and `GdkWayland`-specific APIs; use only
  `GdkDisplay` / `GdkScreen` abstractions.
- Use `gdk_screen_get_resolution()` only for DPI queries.

---

## 8. File Group Summary

| File group | New files |
|---|---|
| App entry | `src/main_linux.cpp` |
| Audio platform | `src/linux/AudioEngine.h/.cpp`, `src/linux/JackPortEnum.h/.cpp` |
| Settings | `src/linux/AppSettings.h/.cpp` |
| UI shell | `src/linux/MainWindow.h/.cpp`, `src/linux/ControlBar.h/.cpp` |
| Plugin UX | `src/linux/PluginSlot.h/.cpp`, `src/linux/PluginDialog.h/.cpp`, `src/linux/ParameterPanel.h/.cpp` |
| Settings dialog | `src/linux/SettingsDialog.h/.cpp` |
| UI resources | `src/linux/ui/main_window.ui`, `src/linux/ui/plugin_slot.ui`, `src/linux/opiqo.css`, `src/linux/opiqo.gresource.xml` |
| Build | `CMakePresets.json` (new preset), `CMakeLists.txt` (new target block) |

---

## 9. Test Plan Summary

### Functional tests
- App startup with valid and missing saved JACK ports.
- Power toggle on/off cycles (50 repetitions).
- JACK port switch while running.
- Add/delete plugins in all 4 slots.
- Parameter changes under active audio.
- Recording for WAV, MP3, and OGG while all slots are active.

### Robustness tests
- USB audio device unplug/replug while JACK graph is active.
- Rapid Power toggle spam (100×).
- JACK server kill and restart while engine is Running.
- Open Settings and Apply repeatedly during idle and Running states.

### Performance tests
- CPU usage: no plugins, 1 plugin, 4 plugins.
- Xrun count measured over 10 minutes at 48 kHz / 256-frame buffer.
- Latency measurement comparing 256-frame and 1024-frame buffer sizes.

---

## 10. Prerequisites

Install the following packages before building:

### Debian / Ubuntu
```bash
sudo apt install \
  libgtk-3-dev \
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
  gtk3-devel \
  jack-audio-connection-kit-devel \
  lilv-devel \
  libsndfile-devel \
  lame-devel \
  opus-devel \
  opusenc-devel \
  cmake make pkg-config
```

---

## 11. Sequencing Recommendation

Implement in this order to prove audio first, then build UI on top:

1. **Milestones 0–3** — Build wiring → JACK pass-through → DSP integration.
2. **Milestones 4–6** — Transport state machine → shell layout → plugin management.
3. **Milestones 7–9** — Parameter UI → recording → settings.
4. **Milestone 10** — Hardening and release.
