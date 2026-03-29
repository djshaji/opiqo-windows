# Opiqo Windows Host Implementation Roadmap

## 1. Objective

Build a production-ready Windows LV2 host that:
- runs low-latency duplex audio with WASAPI,
- processes audio through LiveEffectEngine in real time,
- provides a Win32 desktop UI with 4 plugin slots in a 2x2 layout,
- supports device selection, transport power toggle, and recording.

Primary runtime path:
Audio input -> LiveEffectEngine::process(input, output, frames) -> Audio output

## 2. Scope and Non-Goals

In scope:
- Win32 main window, dialogs, status bar, and control bar.
- WASAPI shared/exclusive duplex stream management.
- Input/output endpoint selection and hot-plug handling.
- Power toggle state machine (Off, Starting, Running, Stopping, Error).
- Plugin add/delete/enable/parameter UI mapped to existing LiveEffectEngine APIs.
- Recording controls with format and quality selection.

Out of scope for first release:
- Advanced theming and skinning.
- MIDI mapping and automation lanes.
- Sandboxed plugin process isolation.

## 3. Architecture Mapping

Layers:
1. UI Layer (Win32)
	 - MainWindow, PluginSlot, PluginDialog, ControlBar, SettingsDialog
2. Audio Platform Layer
	 - AudioEngine (WASAPI), WasapiDeviceEnum
3. DSP Host Layer
	 - LiveEffectEngine and LV2Plugin integration

Ownership:
- AudioEngine owns COM/WASAPI resources and the real-time audio thread.
- LiveEffectEngine owns plugin graph state and processing.
- UI owns user intents, persisted preferences, and state presentation.

## 4. Milestone Plan

## Milestone 0: Project Skeleton and Build Wiring

Goals:
- Create Windows-specific source structure and compile target.

Tasks:
- Add source files listed in UI plan under src/win32 and src/main_win32.cpp.
- Update CMake target for MinGW cross-build with required Windows libs.
- Add resource files (resource.h, app.rc) and a minimal menu.

Deliverables:
- App launches to a blank or minimal Win32 window.
- Build succeeds with cross-toolchain on Linux.

Acceptance criteria:
- Clean compile in Debug.
- Binary runs on Windows without missing DLL/runtime errors.

## Milestone 1: WASAPI Device Enumeration and Selection Model

Goals:
- Build stable endpoint discovery and selection storage.

Tasks:
- Implement WasapiDeviceEnum with:
	- capture/render listing,
	- endpoint id + friendly name + default flag,
	- change notifications (IMMNotificationClient).
- Define AppSettings model:
	- inputDeviceId, outputDeviceId, sampleRate, blockSize, shareMode, recordFormat, recordQuality.
- Implement load/save settings at startup/shutdown.

Deliverables:
- Device list available for UI menu and status bar comboboxes.

Acceptance criteria:
- Device hot-plug refreshes list.
- Missing saved device falls back to default endpoint.

## Milestone 2: AudioEngine Core (No DSP Yet)

Goals:
- Prove full duplex WASAPI pipeline and stream lifecycle.

Tasks:
- Implement AudioEngine start/stop API with parameters:
	- sampleRate, blockSize, inputDeviceId, outputDeviceId, shareMode.
- Implement initialization sequence per WASAPI plan:
	- CoInitializeEx, device activation, format negotiation, event callback mode.
- Implement dedicated real-time audio thread with no allocations in loop.
- Implement basic pass-through path without LiveEffectEngine processing.

Deliverables:
- Stable start/stop and low-glitch pass-through stream.

Acceptance criteria:
- 15-minute pass-through run without crash.
- Stop/start repeated 50 times without leaked handles.

## Milestone 3: LiveEffectEngine Integration in Audio Thread

Goals:
- Insert DSP host into capture->render loop.

Tasks:
- Replace pass-through with LiveEffectEngine::process call.
- Ensure buffer conversion path supports float and fallback format conversion.
- Verify channel handling for stereo path.
- Validate bypass behavior and plugin chain order in process.

Deliverables:
- Real-time processed output with existing engine APIs.

Acceptance criteria:
- With no plugins loaded, audible clean pass-through.
- With plugins loaded, audible effect processing in slot order.

## Milestone 4: Power Toggle State Machine and Transport Control

Goals:
- Make power toggle the source of truth for engine transport state.

Tasks:
- Implement atomic engine state variable: Off, Starting, Running, Stopping, Error.
- Wire ControlBar power toggle:
	- On -> start with selected endpoints.
	- Off -> stop stream.
- Enforce effective-state UI behavior:
	- Keep toggle On only after successful start.
	- Revert to Off on start failure.

Deliverables:
- Reliable and deterministic transport behavior.

Acceptance criteria:
- Duplicate On/Off requests are ignored safely.
- Error path transitions to Error and exposes message in status bar.

## Milestone 5: MainWindow Layout and 2x2 Plugin Slot Grid

Goals:
- Implement required desktop layout and resizing.

Tasks:
- Create main frame with menu and status bar.
- Implement 2x2 slot region:
	- top row: Slot 1, Slot 2
	- bottom row: Slot 3, Slot 4
- Add ControlBar with Power, Gain, Record, Wav format dropdown.
- Add status widgets for Input and Output device selection.

Deliverables:
- Full shell layout matching UI plan.

Acceptance criteria:
- Layout remains usable at minimum size 900 x 650.
- Resizing preserves slot proportions and control visibility.

## Milestone 6: Plugin Management UI

Goals:
- Connect plugin lifecycle controls to existing engine APIs.

Tasks:
- Implement PluginDialog data source from getAvailablePlugins().
- Implement per-slot controls:
	- add plugin,
	- bypass/enable,
	- delete plugin.
- Reflect active plugin name and status in slot header.

Deliverables:
- End-to-end plugin add/delete/enable from UI.

Acceptance criteria:
- Each slot independently manages plugin instance.
- UI remains responsive during plugin operations.

## Milestone 7: Dynamic Parameter Panels

Goals:
- Build runtime-generated controls from plugin metadata.

Tasks:
- Generate controls by port type:
	- control -> slider,
	- toggled -> checkbox,
	- dropdown -> combobox,
	- atom file path -> browse control.
- Implement value mapping min/max/default and on-change updates.
- Ensure real-time-safe parameter handoff path.

Deliverables:
- Fully dynamic parameter UI for loaded plugins.

Acceptance criteria:
- Control movements update sound in real time.
- No audible glitches from rapid slider moves.

## Milestone 8: Recording UX and Format Handling

Goals:
- Complete recording workflow from UI.

Tasks:
- Implement record button start/stop behavior.
- Bind record format dropdown (WAV/MP3/OGG).
- Show/hide quality selector for lossy formats.
- Connect selected format/quality to startRecording arguments.

Deliverables:
- Record transport that works during live processing.

Acceptance criteria:
- Recorded files open successfully in external players.
- Record can run while plugins process in real time.

## Milestone 9: Settings Dialog and Runtime Reconfiguration

Goals:
- Enable configurable audio preferences and preset import/export.

Tasks:
- Implement audio settings tab:
	- sample rate, block size, share mode,
	- explicit input/output endpoint pickers.
- Apply flow:
	- if running, stop engine,
	- apply settings,
	- restart with new values.
- Implement preset export/import using getPresetList and restore path.

Deliverables:
- Full settings workflow and persistent preferences.

Acceptance criteria:
- Changes survive app restart.
- Unsupported setting combinations fail gracefully with fallback.

## Milestone 10: Hardening, Performance, and Release Readiness

Goals:
- Production stability and diagnosability.

Tasks:
- Add rate-limited error logging and user-facing status messages.
- Add MMCSS priority setup for audio thread.
- Add long-run soak tests and device hot-plug stress tests.
- Verify no blocking operations in audio loop.

Deliverables:
- Release candidate build with validation report.

Acceptance criteria:
- 60-minute continuous run without dropout under normal load.
- Device loss/recovery path works without app restart.

## 5. Cross-Cutting Technical Requirements

Real-time safety:
- No heap allocations inside audio callback loop.
- No blocking UI locks inside audio loop.
- Preallocate conversion and transfer buffers.

Threading:
- UI thread handles window messages and user actions only.
- AudioEngine thread handles capture/process/render only.
- Long operations avoid blocking UI responsiveness.

Error handling:
- Surface all critical failures in status bar and logs.
- On start failure, keep app in safe Off state.
- Handle invalidated devices with recoverable workflow.

## 6. Suggested Work Breakdown by File Group

UI shell:
- src/win32/MainWindow.h/.cpp
- src/win32/ControlBar.h/.cpp
- src/win32/resource.h
- src/win32/app.rc

Audio platform:
- src/win32/AudioEngine.h/.cpp
- src/win32/WasapiDeviceEnum.h/.cpp

Plugin UX:
- src/win32/PluginSlot.h/.cpp
- src/win32/PluginDialog.h/.cpp

Settings and app entry:
- src/win32/SettingsDialog.h/.cpp
- src/main_win32.cpp

## 7. Test Plan Summary

Functional tests:
- App startup with valid and missing saved devices.
- Power toggle on/off cycles.
- Input/output device switch while running.
- Add/delete plugins in all 4 slots.
- Parameter changes under active audio.
- Recording for each supported format.

Robustness tests:
- USB headset unplug/replug while running.
- Rapid power toggle spam.
- Open settings and apply repeatedly during idle and running states.

Performance tests:
- CPU usage with no plugins, one plugin, four plugins.
- Glitch/dropout count under stress.
- Latency measurements in shared vs exclusive mode.

## 8. Sequencing Recommendation

Recommended implementation order:
1. Milestones 0-3 (audio foundation first).
2. Milestones 4-6 (transport + core UI controls).
3. Milestones 7-9 (dynamic plugin UX, recording, settings).
4. Milestone 10 (hardening and release).

This order minimizes risk by proving the real-time audio path early before investing heavily in UI complexity.
