# Opiqo Android Host Implementation Roadmap

## 1. Objective

Build a production-ready Android LV2 host that:
- runs low-latency duplex audio with Oboe,
- processes audio through LiveEffectEngine in real time,
- provides a Java/XML Android UI with 4 plugin slots,
- supports plugin discovery, transport power toggle, parameter control, presets, and recording.

Primary runtime path:

Audio input -> Oboe callback -> LiveEffectEngine::process(input, output, frames) -> Audio output

## 2. Scope and Non-Goals

In scope:
- Android Studio / Gradle app module.
- Native C++ engine library via JNI.
- Oboe full-duplex audio engine.
- Java-based UI using Activities, dialogs, and XML layouts.
- Bundled LV2 plugin asset extraction and runtime scanning.
- Plugin add/delete/enable flows and dynamic parameter UI.
- Preset import/export and recording using Storage Access Framework.

Out of scope for first release:
- Background audio service / foreground service processing.
- MIDI mapping and automation lanes.
- Plugin sandboxing in a separate process.
- Compose/Kotlin UI rewrite.
- Advanced theming beyond a functional mobile UI.

## 3. Architecture Mapping

Layers:
1. UI Layer (Android Java)
   - MainActivity
   - PluginSlotView
   - PluginDialog
   - ParameterPanel
   - AppSettings
2. JNI Boundary
   - AudioEngine.java
   - jni_bridge.cpp
3. Audio Platform Layer
   - AudioEngine (Oboe)
4. DSP Host Layer
   - LiveEffectEngine
   - LV2Plugin
   - FileWriter
   - LockFreeQueue

Ownership:
- Java UI owns lifecycle, user actions, storage permissions, and document picker flows.
- JNI bridge owns the native host object and marshals data between Java and C++.
- AudioEngine owns Oboe stream lifecycle and realtime callback state.
- LiveEffectEngine owns plugin graph state, processing, presets, and recording.
- FileWriter owns active recording encoder state.

## 4. Shared Code Reuse Strategy

Reuse with minimal changes:
- src/LiveEffectEngine.h/.cpp
- src/LV2Plugin.hpp
- src/FileWriter.h/.cpp
- src/LockFreeQueue.h/.cpp
- src/AudioBuffer.h
- src/lv2_ringbuffer.h
- src/resample.c
- src/speex_resampler.h
- src/json.hpp

Platform-specific replacements:
- src/win32/AudioEngine.* -> Android Oboe AudioEngine
- src/win32/MainWindow.* -> MainActivity and Android views
- src/win32/AppSettings.* -> SharedPreferences-backed AppSettings.java
- src/main_win32.cpp -> Android app entry / JNI bootstrap

Shared code adjustments expected:
- src/logging_macros.h should route Android logs to logcat via __android_log_print
- Android-safe writable paths must be supplied for LV2 bundle extraction and cached files
- Any dynamic library path assumptions in LiveEffectEngine.cpp should be validated on Android

## 5. Milestone Plan

### Milestone 0: Android Project Skeleton and Build Wiring

Goals:
- Create a buildable Android shell with Java UI and native C++ integration.

Tasks:
- Create android/settings.gradle, android/build.gradle, android/app/build.gradle.
- Create android/app/src/main/AndroidManifest.xml.
- Create android/app/src/main/cpp/CMakeLists.txt.
- Configure externalNativeBuild for the shared native library.
- Add ABI filters:
  - armeabi-v7a
  - arm64-v8a
  - x86_64
- Set minSdkVersion for modern Oboe support.
- Create a minimal MainActivity.java with a blank layout.
- Load the native library from AudioEngine.java.

Deliverables:
- App builds as a debug APK.
- App launches to a blank Android screen.
- Native library loads successfully.

Acceptance criteria:
- ./gradlew assembleDebug succeeds.
- App installs and starts on a device/emulator without UnsatisfiedLinkError.

### Milestone 1: Native Dependency Packaging

Goals:
- Make all native LV2/media dependencies available for Android builds.

Tasks:
- Build Android-compatible archives or shared libs for:
  - lilv
  - serd
  - sord
  - sratom
  - zix
  - sndfile
  - lame
  - opus
  - opusenc
  - flac
  - ogg
  - vorbis
- Choose one dependency strategy:
  - checked-in prebuilts under android/app/src/main/jniLibs or a sibling third_party directory
  - reproducible build script under tools/android
- Add headers and library search paths to app/src/main/cpp/CMakeLists.txt.
- Document the expected output locations for all ABIs.

Deliverables:
- Native dependency set available for all target ABIs.
- CMake can resolve headers and link libraries.

Acceptance criteria:
- Native build links successfully for every configured ABI.
- Dependency paths are deterministic and documented.

### Milestone 2: Oboe AudioEngine Core

Goals:
- Prove duplex Oboe stream lifecycle and pass-through audio.

Tasks:
- Create android/app/src/main/cpp/AudioEngine.h/.cpp.
- Implement:
  - start(sampleRate, blockSize)
  - stop()
  - state()
  - sampleRate()
  - blockSize()
  - errorMessage()
  - xrunCount()
  - setLiveEngine(LiveEffectEngine*)
- Configure Oboe for low-latency float audio.
- Implement realtime callback with pass-through before DSP integration.
- Add atomic state transitions:
  - Off
  - Starting
  - Running
  - Stopping
  - Error
- Handle stream errors and device disconnects.

Deliverables:
- Start/stop works for duplex audio.
- Pass-through is audible without DSP.

Acceptance criteria:
- Repeated start/stop cycles do not leak streams.
- Audio callback performs without allocations or crashes.

### Milestone 3: LiveEffectEngine Integration

Goals:
- Insert the existing DSP host into the Oboe callback.

Tasks:
- Replace pass-through with LiveEffectEngine::process().
- Preserve stereo interleaved float buffer flow.
- Confirm block size and sample rate are propagated to LiveEffectEngine.
- Validate bypass behavior when no plugins are loaded.
- Expose xrun count and error state back to Java.

Deliverables:
- Realtime audio flows through LiveEffectEngine on Android.

Acceptance criteria:
- No-plugin case gives clean pass-through.
- Loaded plugin is audible in slot order.
- Realtime path stays stable under parameter changes.

### Milestone 4: JNI Bridge and Native Host Ownership

Goals:
- Expose all required engine functionality to Java through a stable JNI API.

Tasks:
- Create android/app/src/main/cpp/jni_bridge.cpp.
- Define one native host object that owns:
  - LiveEffectEngine
  - AudioEngine
  - Android-specific extraction/config paths
- Expose JNI methods for:
  - nativeCreate
  - nativeDestroy
  - nativeInitPlugins
  - nativeAddPlugin
  - nativeDeletePlugin
  - nativeSetPluginEnabled
  - nativeSetValue
  - nativeSetFilePath
  - nativeStartAudio
  - nativeStopAudio
  - nativeGetEngineState
  - nativeGetEngineError
  - nativeGetXrunCount
  - nativeGetAvailablePlugins
  - nativeGetPluginPortInfo
  - nativeGetWritableParams
  - nativeGetPresetList
  - nativeApplyPreset
  - nativeSetGain
  - nativeStartRecording
  - nativeStopRecording
- Use JSON strings for complex data returned to Java.

Deliverables:
- Java can control audio, plugins, presets, and recording entirely via JNI.

Acceptance criteria:
- JNI method coverage matches the UI requirements.
- No lifecycle leaks after repeated create/destroy cycles.

### Milestone 5: LV2 Asset Extraction and Plugin Discovery

Goals:
- Bundle LV2 plugins in the APK and make them discoverable at runtime.

Tasks:
- Store LV2 bundles in app assets.
- Extract them on first launch or app version change to an app-owned writable directory.
- Track extraction version metadata in SharedPreferences.
- Pass the extracted LV2 directory to LiveEffectEngine::initPlugins().
- Surface plugin scan results to Java as JSON.

Deliverables:
- Plugin catalog populated from extracted assets.

Acceptance criteria:
- First-run extraction succeeds.
- Plugin scan returns a usable catalog with metadata.
- Re-launch does not re-extract unnecessarily.

### Milestone 6: Java Facade and App Settings

Goals:
- Define the Java API that the UI will use.

Tasks:
- Create android/app/src/main/java/com/opiqo/app/AudioEngine.java.
- Load the native library.
- Expose thin wrappers around the JNI methods.
- Define engine state constants or enum.
- Create android/app/src/main/java/com/opiqo/app/AppSettings.java.
- Store:
  - gain
  - record format
  - record quality
  - last known extraction version
  - optional routing preferences
  - last preset URI if desired
- Use SharedPreferences for lightweight persistence.

Deliverables:
- Java-side app logic can interact with the native engine without direct JNI use elsewhere.

Acceptance criteria:
- AudioEngine.java is the only JNI entry point required by UI code.
- Settings persist across app restarts.

### Milestone 7: Main UI Shell

Goals:
- Create the Android UI frame for transport and plugin management.

Tasks:
- Create MainActivity.java and activity_main.xml.
- Add:
  - Power toggle/button
  - Record button
  - Gain control
  - Status text
  - Xrun display
  - 4 plugin slot containers
- Implement periodic polling or lifecycle-safe refresh of engine state from Java.
- Keep the UI responsive during plugin and audio actions.

Deliverables:
- A functional shell that can start/stop audio and display four plugin slots.

Acceptance criteria:
- UI loads reliably.
- Engine state is visible and updates correctly.
- Power control reflects actual native state.

### Milestone 8: Plugin Management UI

Goals:
- Support plugin loading, bypass, and deletion per slot.

Tasks:
- Create PluginSlotView or equivalent custom view/controller.
- Add slot header controls:
  - Add
  - Bypass
  - Delete
- Create PluginDialog backed by getAvailablePlugins() JSON.
- Add search/filter over plugin list.
- On plugin selection:
  - call nativeAddPlugin(slot, uri)
  - refresh displayed plugin metadata
  - rebuild the parameter panel

Deliverables:
- Each slot independently manages one plugin instance.

Acceptance criteria:
- Plugins can be added and removed from all four slots.
- Bypass toggles effect audibly without crashing.
- UI stays responsive during plugin graph changes.

### Milestone 9: Dynamic Parameter Panels

Goals:
- Render controls dynamically from LV2 metadata.

Tasks:
- Create ParameterPanel.java.
- Parse nativeGetPluginPortInfo(slot) JSON.
- Map control types to Android widgets:
  - Float range -> SeekBar
  - Enum -> Spinner
  - Toggle -> Switch
  - Trigger -> Button
  - Atom file path -> Button + SAF file picker
- Wire widget changes to nativeSetValue or nativeSetFilePath.
- Rebuild the panel when the plugin changes.

Deliverables:
- Plugin-specific controls are generated at runtime.

Acceptance criteria:
- Parameter changes take effect immediately.
- File-path parameters can be supplied from the document picker.
- No stale controls remain after a plugin is replaced or deleted.

### Milestone 10: Recording UX and File Flow

Goals:
- Enable recording from the Android UI using existing FileWriter behavior.

Tasks:
- Add record format and quality selectors.
- Launch SAF create-document flow for output file selection.
- Open a ParcelFileDescriptor in Java.
- Pass the raw fd into nativeStartRecording.
- Call nativeStopRecording on stop.
- Reflect recording state in the UI.

Deliverables:
- Recording works without adding path-based storage assumptions to native code.

Acceptance criteria:
- Recorded files finalize cleanly.
- WAV/MP3/OGG outputs open in external players.
- Recording works while plugins are active.

### Milestone 11: Preset Import/Export

Goals:
- Support preset save/load using existing engine JSON APIs.

Tasks:
- Export:
  - call nativeGetPresetList()
  - write JSON to a user-selected SAF URI
- Import:
  - open a picked JSON document
  - read it in Java
  - pass it to native code
  - apply preset per slot
- Define behavior when a preset references a missing plugin:
  - recommended: partial apply with visible per-slot warnings

Deliverables:
- User can round-trip preset state through JSON files.

Acceptance criteria:
- Exported preset imports correctly after clearing the graph.
- Missing plugins do not crash the import flow.

### Milestone 12: Lifecycle and Error Hardening

Goals:
- Make the Android port stable under normal mobile lifecycle events.

Tasks:
- Handle:
  - Activity recreation
  - rotation/configuration changes
  - background/foreground transitions
  - permission denial or revocation
  - device route changes
  - Oboe error callbacks
- Decide the initial lifecycle policy:
  - recommended first release: stop audio on background
- Add rate-limited error propagation from native to Java.
- Expose engine diagnostics in the UI.

Deliverables:
- Predictable behavior across common Android lifecycle edges.

Acceptance criteria:
- No leaked native resources after background/foreground cycles.
- Error state is surfaced clearly to the user.
- App can recover from repeated transport failures.

### Milestone 13: Hardening, Validation, and Release Readiness

Goals:
- Verify correctness, stability, and packaging before release.

Tasks:
- Test plugin scan and extraction on clean install.
- Test all four slots with live add/delete/bypass flows.
- Test dynamic parameter updates under active audio.
- Test recording in each supported format.
- Test preset export/import.
- Test repeated start/stop cycles.
- Test on at least one physical low-latency device.
- Measure xrun counts and capture logcat diagnostics.
- Review APK size impact from bundled LV2/media dependencies.

Deliverables:
- Release candidate build plus validation notes.

Acceptance criteria:
- Repeated transport cycles do not leave the engine stuck.
- Plugin catalog loads reliably on target devices.
- Audio remains functional after long-run testing.

## 6. Android-Specific Technical Requirements

Realtime safety:
- No allocations in the Oboe audio callback.
- No blocking UI calls inside the audio callback.
- Parameter changes must use safe handoff paths already established in the shared engine.

Threading:
- Main thread handles views, permissions, document picker, and dialog flows.
- Native audio thread handles capture/process/render only.
- JNI calls from UI must not stall the callback thread.

Storage:
- Use app-owned directories for extracted LV2 assets.
- Use Storage Access Framework for user-chosen recordings and presets.
- Prefer file descriptors over raw filesystem paths when crossing into native code.

Packaging:
- All required LV2/media libraries must exist for every shipped ABI.
- LV2 plugin assets must be versioned so stale extracted copies can be refreshed.

## 7. Suggested Work Breakdown by File Group

Android project shell:
- android/settings.gradle
- android/build.gradle
- android/app/build.gradle
- android/app/src/main/AndroidManifest.xml

Native audio/JNI:
- android/app/src/main/cpp/CMakeLists.txt
- android/app/src/main/cpp/AudioEngine.h/.cpp
- android/app/src/main/cpp/jni_bridge.cpp

Java app layer:
- android/app/src/main/java/com/opiqo/app/AudioEngine.java
- android/app/src/main/java/com/opiqo/app/MainActivity.java
- android/app/src/main/java/com/opiqo/app/AppSettings.java

Plugin UI:
- android/app/src/main/java/com/opiqo/app/PluginDialog.java
- android/app/src/main/java/com/opiqo/app/PluginSlotView.java
- android/app/src/main/java/com/opiqo/app/ParameterPanel.java

Resources:
- android/app/src/main/res/layout/activity_main.xml
- android/app/src/main/res/layout/dialog_plugin.xml
- android/app/src/main/res/layout/view_plugin_slot.xml
- android/app/src/main/res/layout/view_parameter_row.xml

## 8. Sequencing Recommendation

Recommended implementation order:
1. Milestones 0-2: Android shell, native dependencies, Oboe pass-through.
2. Milestones 3-5: JNI, shared engine integration, plugin extraction/scanning.
3. Milestones 6-9: Java facade, settings, slot UI, dynamic parameter rendering.
4. Milestones 10-13: recording, presets, lifecycle hardening, release validation.

This order proves the audio and native dependency foundation first, before investing in the higher-level Android UI and preset flows.