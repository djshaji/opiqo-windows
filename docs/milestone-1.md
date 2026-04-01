# Milestone 1 — WASAPI Device Enumeration and Selection Model

## New files

### `src/win32/AppSettings.h`
Defines the `AppSettings` struct with all user-configurable engine parameters:

| Field | Type | Default |
|---|---|---|
| `inputDeviceId` | `std::string` | `""` (resolved at startup) |
| `outputDeviceId` | `std::string` | `""` (resolved at startup) |
| `sampleRate` | `int` | `48000` |
| `blockSize` | `int` | `4096` |
| `exclusiveMode` | `bool` | `false` |
| `recordFormat` | `int` | `0` (WAV) |
| `recordQuality` | `int` | `0` (default/CBR 128) |

Declares `AppSettings::load()` and `AppSettings::save()`.

### `src/win32/AppSettings.cpp`
- Persistence path: `%APPDATA%\Opiqo\settings.json`.
- `load()` reads and deserialises the JSON file using the bundled `json.hpp`; returns struct with defaults on any error (missing file, parse failure).
- `save()` creates `%APPDATA%\Opiqo\` if absent, then writes pretty-printed JSON. Write errors are silently ignored to avoid disrupting shutdown.

---

## Changed files

### `src/win32/WasapiDeviceEnum.h`
Replaced the empty stub with a pimpl-based class:

- Constructor requires COM to have been initialised on the calling thread.
- `enumerateInputDevices()` / `enumerateOutputDevices()` — now return real `DeviceInfo` lists via `IMMDeviceEnumerator::EnumAudioEndpoints`.
- `setChangeCallback(std::function<void()>)` — registers an `IMMNotificationClient` that fires the callback on any device topology change (add, remove, state change, default change). The callback runs on the COM notification thread; callers should marshal to the UI thread via `PostMessage`.
- `resolveOrDefault(list, savedId)` — static helper: returns `savedId` if present in `list`, falls back to the default-flagged device, then the first device, then `""`.

### `src/win32/WasapiDeviceEnum.cpp`
Full implementation:

- `INITGUID` defined in this translation unit to resolve `CLSID_MMDeviceEnumerator` and `IID_IMMDeviceEnumerator` without `-linitguid`.
- `enumerateFlow()` — internal helper that resolves the default endpoint id for a given `EDataFlow`, then iterates the active collection reading `PKEY_Device_FriendlyName` from each endpoint's `IPropertyStore`.
- `NotificationClientImpl` — `IMMNotificationClient` COM object (ref-counted). Routes `OnDeviceAdded`, `OnDeviceRemoved`, `OnDeviceStateChanged`, and `OnDefaultDeviceChanged` to the registered `std::function`. Registered and unregistered via `IMMDeviceEnumerator`.

### `src/win32/MainWindow.h`
- Added `#include` for `AppSettings.h` and `WasapiDeviceEnum.h`.
- Added destructor.
- Added private member `settings_` (`AppSettings`).
- Added private member `deviceEnum_` (`std::unique_ptr<WasapiDeviceEnum>`).
- Added private method `onDeviceListChanged()`.

### `src/win32/MainWindow.cpp`
- `create()` calls `CoInitializeEx(COINIT_APARTMENTTHREADED)` before anything else; returns `false` on failure.
- `AppSettings::load()` called immediately after COM init.
- `WasapiDeviceEnum` constructed and change callback registered; the callback posts `WM_APP+1` to the main window to keep the notification on the UI thread.
- `onDeviceListChanged()` called once during `create()` to resolve saved device ids against the live endpoint list.
- `handleMessage()` handles `WM_APP+1` by calling `onDeviceListChanged()`.
- `WM_DESTROY` handler: calls `settings_.save()`, tears down `deviceEnum_` (releases COM objects), then calls `CoUninitialize()` before `PostQuitMessage`.

### `CMakeLists.txt`
- Added `src/win32/AppSettings.cpp` to `WIN32_UI_SOURCES`.

---

## Acceptance criteria status

| Criterion | Status |
|---|---|
| Device hot-plug refreshes list | Met — `IMMNotificationClient` posts to UI thread on any topology change |
| Missing saved device falls back to default endpoint | Met — `resolveOrDefault()` implements the three-tier fallback |
| Settings survive app restart | Met — load on startup, save on `WM_DESTROY` |
| Clean MinGW cross-compile | Met — `make` exits 0 with no warnings |
