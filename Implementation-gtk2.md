# Opiqo Linux Host Implementation Plan (GTK2 + JACK)

## 1. Objective

Build a native Linux port of the Opiqo LV2 plugin host that:
- runs low-latency duplex audio via the JACK Audio Connection Kit,
- processes audio through the existing `LiveEffectEngine` in real time,
- provides a GTK2 desktop UI with 4 plugin slots in a 2×2 layout,
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
| UI toolkit | Win32 API | GTK2 |
| Audio backend | WASAPI | JACK (libjack2) |
| Device discovery | IMMDeviceEnumerator (COM) | `jack_get_ports()` |
| Hot-plug events | IMMNotificationClient | `jack_set_port_registration_callback()` |
| Settings storage | `%APPDATA%\Opiqo\settings.json` | `~/.config/opiqo/settings.json` (XDG) |
| Build | MinGW cross-compile | Native CMake, pkg-config |
| UI definition | Win32 resources (.rc) | GtkBuilder .ui XML (GTK 2.12+) |

### Why GTK2?

- GTK2 has near-universal availability on every Linux distribution, including
  older embedded and studio distros that ship JACK but not GTK3/GTK4.
- The GTK2 widget set (`GtkHBox`/`GtkVBox`, `GtkTable`, `GtkComboBox`,
  `GtkHScale`) maps cleanly to the Win32 controls already in the Win32 port.
- GTK2 runtime footprint is smaller, which benefits low-latency audio machines
  that avoid unnecessary services.

### Why JACK instead of PipeWire-native or ALSA?

- JACK provides a guaranteed real-time callback (`jack_process_callback`) with
  float32 buffers matching `LiveEffectEngine`'s expected format — zero
  conversion required.
- PipeWire ships a full JACK compatibility layer (`pipewire-jack`); the same
  binary works on both classic JACK2 and modern PipeWire systems.
- ALSA would require manual latency management and ring-buffer design already
  solved by JACK.

---

## 3. GTK2-Specific API Notes

These are the key GTK2 differences to keep in mind across all milestones:

| Feature | GTK3/4 equivalent | GTK2 API |
|---|---|---|
| App object | `GtkApplication` | `gtk_init()` + `gtk_main()` |
| Main window | `GtkApplicationWindow` | `GtkWindow` (`GTK_WINDOW_TOPLEVEL`) |
| Header bar | `GtkHeaderBar` | `GtkMenuBar` inside a `GtkVBox` |
| Grid layout | `GtkGrid` | `GtkTable` (`gtk_table_new(rows, cols, homogeneous)`) |
| Horizontal box | `GtkBox` (horizontal) | `GtkHBox` (`gtk_hbox_new()`) |
| Vertical box | `GtkBox` (vertical) | `GtkVBox` (`gtk_vbox_new()`) |
| Status bar | `GtkStatusbar` | `GtkStatusbar` (unchanged) |
| Text combo | `GtkComboBoxText` | `GtkComboBox` with a `GtkListStore` |
| Search entry | `GtkSearchEntry` | `GtkEntry` with an embedded icon via `gtk_entry_set_icon_from_stock()` |
| CSS theming | `GtkCssProvider` | `gtk_rc_parse_string()` using GTK RC syntax |
| File chooser | `GtkFileChooserDialog` | `GtkFileChooserDialog` (available since GTK 2.4, API unchanged) |
| Cross-thread UI | `g_idle_add()` (safe) | `g_idle_add()` — GTK2 requires GDK lock for direct calls: `gdk_threads_enter()` / `gdk_threads_leave()`. Prefer `g_idle_add()` to avoid lock management. |

---

## 4. Source Layout

New files are placed in `src/linux/` to mirror `src/win32/`.

```
src/
  main_linux.cpp              ← gtk_init() / gtk_main() entry point
  linux/
    AppSettings.h/.cpp        ← XDG config load/save
    AudioEngine.h/.cpp        ← JACK client lifecycle and callback
    JackPortEnum.h/.cpp       ← JACK port discovery (replaces WasapiDeviceEnum)
    MainWindow.h/.cpp         ← GtkWindow + GtkTable layout
    ControlBar.h/.cpp         ← GtkHBox: power, gain, record, format
    PluginSlot.h/.cpp         ← GtkFrame per slot
    PluginDialog.h/.cpp       ← GtkDialog: plugin browser
    ParameterPanel.h/.cpp     ← Dynamic GTK2 controls from PortInfo
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

## 5. Build System Changes

Add a new CMake preset and target alongside the existing Windows preset.

### New preset in `CMakePresets.json`

```json
{
  "name": "linux-gtk2",
  "displayName": "Linux (GTK2 + JACK)",
  "binaryDir": "${sourceDir}/build-linux-gtk2",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Release",
    "OPIQO_TARGET_PLATFORM": "linux-gtk2"
  }
}
```

### CMakeLists.txt additions

```cmake
if(OPIQO_TARGET_PLATFORM STREQUAL "linux-gtk2")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK2    REQUIRED gtk+-2.0)
  pkg_check_modules(JACK    REQUIRED jack)
  pkg_check_modules(LILV    REQUIRED lilv-0)
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

  target_compile_definitions(opiqo PRIVATE
    ${GTK2_CFLAGS_OTHER}
    # Suppress GDK_DISABLE_DEPRECATED if old stock icon APIs are used
  )

  target_include_directories(opiqo PRIVATE
    ${GTK2_INCLUDE_DIRS} ${JACK_INCLUDE_DIRS} ${LILV_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS} src/ src/linux/
  )

  target_link_libraries(opiqo PRIVATE
    ${GTK2_LIBRARIES} ${JACK_LIBRARIES} ${LILV_LIBRARIES}
    ${SNDFILE_LIBRARIES} pthread dl
  )
endif()
```

### Build commands

```bash
cmake --preset linux-gtk2
cmake --build build-linux-gtk2 -j$(nproc)
```

---

## 6. Architecture Mapping

```
┌─────────────────────────────────┐
│         GTK2 UI Layer           │
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
- GTK main thread (inside `gtk_main()`) handles all widget events and UI state.
- JACK real-time callback thread runs `LiveEffectEngine::process()`.
- Cross-thread communication uses `LockFreeQueue` (already in codebase) and
  `g_idle_add()` to marshal JACK events back to the GTK main thread without
  requiring `gdk_threads_enter()`/`gdk_threads_leave()`.
- Call `gdk_threads_init()` once before `gtk_init()` as a precaution for any
  third-party code that may call GTK directly from threads.

---

## 7. Milestone Plan

### Milestone 0: Project Skeleton and Build Wiring

**Goals:** Compile a minimal GTK2 window under the new `linux-gtk2` preset.

**Tasks:**
- Add the `linux-gtk2` preset to `CMakePresets.json`.
- Add the `if(OPIQO_TARGET_PLATFORM STREQUAL "linux-gtk2")` block to
  `CMakeLists.txt`.
- Create `src/main_linux.cpp`:
  ```cpp
  #include <gtk/gtk.h>
  int main(int argc, char** argv) {
      gdk_threads_init();
      gtk_init(&argc, &argv);
      GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
      gtk_window_set_title(GTK_WINDOW(win), "Opiqo");
      gtk_widget_show_all(win);
      gtk_main();
      return 0;
  }
  ```
- Add stub header/source files for all `src/linux/` modules.

**Deliverables:** `build-linux-gtk2/opiqo` binary that opens an empty GTK2 window.

**Acceptance criteria:**
- `cmake --preset linux-gtk2 && cmake --build build-linux-gtk2` succeeds without
  errors on a system with GTK2 ≥ 2.24 and JACK ≥ 1.9.
- Binary runs and shows a titled window without crashes.

---

### Milestone 1: JACK Port Enumeration and Settings Model

**Goals:** Discover available JACK client ports and persist user preferences.

**Tasks:**

*`JackPortEnum`*:
- Open a temporary JACK client (`jack_client_open("opiqo-enum", JackNoStartServer, nullptr)`) for probing.
- Implement `enumerateCapturePorts()` and `enumeratePlaybackPorts()` using
  `jack_get_ports()` with `JackPortIsPhysical` flag.
- Return a `PortInfo { std::string id; std::string friendlyName; bool isDefault; }` list.
- Register `jack_set_port_registration_callback()` to fire a
  `std::function<void()>` change callback; post it to the GTK main thread via
  `g_idle_add()`.
- Implement `static std::string resolveOrDefault(const std::vector<PortInfo>&, const std::string& saved)`.

*`AppSettings`*:
- Fields: `capturePort`, `playbackPort`, `sampleRate` (read-only from JACK
  server), `recordFormat`, `recordQuality`, `gain`.
- Load from `$XDG_CONFIG_HOME/opiqo/settings.json`
  (falls back to `~/.config/opiqo/settings.json`); uses the bundled `json.hpp`.
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
int32_t     sampleRate()   const;   // jack_get_sample_rate()
int32_t     blockSize()    const;   // jack_get_buffer_size()
std::string errorMessage() const;
uint64_t    xrunCount()    const;
```

*Internals:*
- `jack_client_open("opiqo", JackNoStartServer, nullptr)`.
- Register one capture port and one playback port via `jack_port_register()`.
- `jack_process_callback`: copy input buffer to output buffer (pass-through)
  without calling `LiveEffectEngine` yet.
- `jack_set_xrun_callback`: increment `xrunCount_` atomically.
- `jack_set_server_lost_callback`: set state to `Error` and post a
  `g_idle_add` notification to the main thread.
- `start()` calls `jack_activate()` then `jack_connect()` to the selected
  physical ports.
- `stop()` calls `jack_deactivate()` then `jack_client_close()`.

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
- JACK's native format is `jack_default_audio_sample_t` (`float`), matching
  `LiveEffectEngine` exactly — no conversion needed.
- Register a second capture and playback port for stereo; remix mono→stereo
  if the loaded plugin requires two channels.
- Validate bypass: with no plugins loaded, signal passes through cleanly.

**Deliverables:** Real-time plugin processing over JACK.

**Acceptance criteria:**
- With no plugins loaded: clean pass-through audible.
- With a plugin loaded in slot 1: effect audible on output.
- No allocations in the JACK callback (verify with `MALLOC_CHECK_=3`).

---

### Milestone 4: Power Toggle State Machine

**Goals:** Make the Power button the single source of truth for engine transport.

**Tasks:**
- Implement `std::atomic<State>` with transitions:
  `Off → Starting → Running`, `Running → Stopping → Off`, any → `Error`.
- Wire `ControlBar` power toggle:
  - toggled On → `AudioEngine::start()` with selected ports.
  - toggled Off → `AudioEngine::stop()`.
- Use `g_timeout_add(200, on_poll_engine_state, this)` to poll
  `AudioEngine::state()` on the GTK main thread and update the toggle and
  status bar.
- On `State::Error`: revert toggle to inactive, push message to `GtkStatusbar`.

**Deliverables:** Reliable and deterministic transport behavior.

**Acceptance criteria:**
- Rapid start/stop spam (50×) leaves engine in a consistent state.
- JACK server crash moves UI to Error state without hang.

---

### Milestone 5: MainWindow Layout and 2×2 Plugin Slot Grid

**Goals:** Build the full GTK2 shell layout.

**Tasks:**

*GTK2 widget hierarchy:*
```
GtkWindow "opiqo"
  GtkVBox (spacing=0)
    GtkMenuBar                      ← File / Settings menus
    GtkTable (2 rows × 2 cols,      ← plugin slots
              homogeneous=TRUE)
      PluginSlot[0]  row=0 col=0
      PluginSlot[1]  row=0 col=1
      PluginSlot[2]  row=1 col=0
      PluginSlot[3]  row=1 col=1
    ControlBar (GtkHBox)            ← power, gain, record, format
    GtkStatusbar "statusBar_"       ← bottom status text
```

- Define the static skeleton in a `main_window.ui` GtkBuilder file
  (GTK 2.12+ supports `GtkBuilder`).
- `MainWindow::create()` calls `gtk_builder_new()` then
  `gtk_builder_add_from_file()`.
- Minimum window size: 900 × 650 via `gtk_window_set_default_size()`.
- Basic slot styling via `gtk_rc_parse_string()` using GTK RC format:
  ```
  style "slot-frame" { bg[NORMAL] = "#2a2a2a" }
  widget "*.slot-frame" style "slot-frame"
  ```
  Apply per-widget with `gtk_widget_set_name()`.

**Deliverables:** Full shell layout with placeholder slot frames.

**Acceptance criteria:**
- Window resizes gracefully; `GtkTable` slots scale proportionally.
- Layout renders correctly on both X11 and through Xwayland.

---

### Milestone 6: Plugin Management UI

**Goals:** Connect plugin lifecycle controls to existing `LiveEffectEngine` APIs.

**Tasks:**

*`PluginSlot` (one `GtkFrame` per slot):*
```
GtkFrame
  GtkVBox
    GtkHBox                    ← header row
      GtkLabel  pluginName_
      GtkButton "+"             addButton_
      GtkToggleButton "Bypass"  bypassButton_
      GtkButton "×"             deleteButton_
    ParameterPanel             ← filled after add
```

*`PluginDialog` (`GtkDialog`):*
- Backed by `LiveEffectEngine::getAvailablePlugins()`.
- Display with `GtkTreeView` + `GtkListStore`
  (two columns: plugin name `G_TYPE_STRING`, URI `G_TYPE_STRING`).
- Filter via a `GtkTreeModelFilter` driven by a plain `GtkEntry` (search box);
  connect the `"changed"` signal to call `gtk_tree_model_filter_refilter()`.
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

**Goals:** Generate GTK2 controls at runtime from `LV2Plugin::PortInfo`.

**Tasks:**

*Control type → GTK2 widget mapping:*

| `PortInfo::ControlType` | GTK2 widget |
|-------------------------|-------------|
| `Float` (range)         | `GtkHScale` (`gtk_hscale_new_with_range(min, max, step)`) |
| `Float` (enumeration)   | `GtkComboBox` with a `GtkListStore` (`G_TYPE_STRING` column) populated with scale-point labels |
| `Toggle`                | `GtkCheckButton` |
| `Trigger`               | `GtkButton` (fires `engine_->setValue(slot, idx, 1.0f)` on `"clicked"`) |
| `AtomFilePath`          | `GtkButton` "Browse…" → `GtkFileChooserDialog` (`GTK_FILE_CHOOSER_ACTION_OPEN`) |

- `ParameterPanel` is a `GtkScrolledWindow`
  (`GTK_POLICY_AUTOMATIC`, `GTK_POLICY_AUTOMATIC`) containing a `GtkVBox`.
- Build loop calls `engine_->getPluginPortInfo(slot)` and appends a row
  (`GtkHBox`: `GtkLabel` + control widget) per port.
- Signal connections:
  - `GtkHScale`: `"value-changed"` → `engine_->setValue()`.
  - `GtkComboBox`: `"changed"` → map active index to scale-point value →
    `engine_->setValue()`.
  - `GtkCheckButton`: `"toggled"` → `engine_->setValue(slot, idx, active ? 1.0f : 0.0f)`.
- All callbacks fire on the GTK main thread — no special marshalling needed.
- Destroy the `GtkVBox` children with `gtk_container_foreach()` +
  `gtk_widget_destroy()` when clearing the panel.

**Deliverables:** Fully dynamic parameter UI for loaded plugins.

**Acceptance criteria:**
- Slider movements update sound in real time without JACK xruns.
- Panels scroll cleanly when a plugin exposes many ports.
- Rebuilding the panel on plugin change leaves no dangling widget references.

---

### Milestone 8: Recording UX and Format Handling

**Goals:** Wire recording controls to `FileWriter` via `LiveEffectEngine`.

**Tasks:**
- `ControlBar` record toggle (`GtkToggleButton`): on →
  `engine_->startRecording(fd, format, quality)`; off →
  `engine_->stopRecording()`.
- Output file path: `g_build_filename(g_get_home_dir(), "Music", timestamp_filename, NULL)`
  (create the directory if it does not exist with `g_mkdir_with_parents()`).
- Format `GtkComboBox` bound to `AppSettings::recordFormat` (0=WAV, 1=MP3, 2=OGG).
- Quality `GtkComboBox` shown via `gtk_widget_show()`/`gtk_widget_hide()` for
  lossy formats only; connected to `"changed"` signal on the format combo.
- Record toggle label changes between "Record" and "Stop" via
  `gtk_button_set_label()`.

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
- JACK capture port `GtkComboBox` populated by `JackPortEnum`.
- JACK playback port `GtkComboBox`.
- Read-only `GtkLabel` for sample rate and buffer size (provided by JACK server).
- "Apply" `GtkButton`: if engine is running, stop → apply settings → restart.

**Presets tab:**
- Export `GtkButton`: `GtkFileChooserDialog` (`GTK_FILE_CHOOSER_ACTION_SAVE`)
  → writes result of `engine_->getPresetList()` to JSON file.
- Import `GtkButton`: `GtkFileChooserDialog` (`GTK_FILE_CHOOSER_ACTION_OPEN`)
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
- Set JACK thread to `SCHED_FIFO` real-time priority via `jack_client_open`
  `JackRealTime` option flag or `pthread_setschedparam` post-activation.
- Add xrun counter displayed in `GtkStatusbar`, updating via the 200 ms
  `g_timeout_add` poll.
- Log xruns and engine errors to `~/.local/share/opiqo/opiqo.log` using the
  existing `logging_macros.h` macros.
- Add device hot-plug stress test: plug/unplug a USB audio device 20× while
  running and confirm reconnect without app restart.
- Verify JACK callback is allocation-free with Valgrind `--tool=massif`.
- Confirm PipeWire-JACK compatibility: run against `pipewire-jack` shim and
  verify identical behaviour to JACK2.

**Deliverables:** Release candidate build with validation report.

**Acceptance criteria:**
- 60-minute soak run without xruns on a quiet system.
- JACK server restart recovers engine without app restart.
- Zero memory leaks reported by Valgrind on clean startup/shutdown.

---

## 8. Cross-Cutting Technical Requirements

### Real-time safety (JACK callback)
- No heap allocations inside `jack_process_callback`.
- No mutexes that can block; use `LockFreeQueue` (already in codebase) for
  plugin parameter updates.
- No GTK calls from the JACK thread; communicate back to UI via `g_idle_add()`.
- Preallocate all conversion and channel-split buffers in `AudioEngine::start()`.

### Thread safety
- GTK2 is **not** thread-safe by default.  All widget operations must run on
  the GTK main thread.
- Call `gdk_threads_init()` once before `gtk_init()`.
- The JACK callback must never call GTK directly; use `g_idle_add()` to post
  work to the main thread.
- `std::atomic<State>` for engine state visible to both threads.

### Error handling
- Surface all JACK errors in `GtkStatusbar` via `g_idle_add`.
- On `jack_set_server_lost_callback`: set `State::Error`, post idle callback,
  revert Power toggle.
- `AppSettings::load()` returns safe defaults on any file error.

### X11 / Xwayland compatibility
- GTK2 renders only via X11; on Wayland systems, Xwayland is required.
- Use `GtkFileChooserDialog` for all file choosers.
- Avoid `GdkX11`-private symbol lookups; use only the public GTK2 API.

---

## 9. File Group Summary

| File group | New files |
|---|---|
| App entry | `src/main_linux.cpp` |
| Audio platform | `src/linux/AudioEngine.h/.cpp`, `src/linux/JackPortEnum.h/.cpp` |
| Settings | `src/linux/AppSettings.h/.cpp` |
| UI shell | `src/linux/MainWindow.h/.cpp`, `src/linux/ControlBar.h/.cpp` |
| Plugin UX | `src/linux/PluginSlot.h/.cpp`, `src/linux/PluginDialog.h/.cpp`, `src/linux/ParameterPanel.h/.cpp` |
| Settings dialog | `src/linux/SettingsDialog.h/.cpp` |
| UI resources | `src/linux/ui/main_window.ui`, `src/linux/ui/plugin_slot.ui`, `src/linux/opiqo.glade` |
| Build | `CMakePresets.json` (new `linux-gtk2` preset), `CMakeLists.txt` (new target block) |

---

## 10. Test Plan Summary

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

## 11. Prerequisites

Install the following packages before building:

### Debian / Ubuntu
```bash
sudo apt install \
  libgtk2.0-dev \
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
  gtk2-devel \
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

Implement in this order to prove audio first, then build UI on top:

1. **Milestones 0–3** — Build wiring → JACK pass-through → DSP integration.
2. **Milestones 4–6** — Transport state machine → shell layout → plugin management.
3. **Milestones 7–9** — Parameter UI → recording → settings.
4. **Milestone 10** — Hardening and release.
