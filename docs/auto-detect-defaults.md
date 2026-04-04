# Auto-detect default devices, sample rate, and block size on startup

## What changed

On first run (or when `settings.json` is missing / has no values for these fields) the program now automatically selects sensible defaults instead of hard-coding 48 000 Hz / 4096 frames.

### `AppSettings.h` / `AppSettings.cpp`

- `sampleRate` default changed from `48000` → `0` (sentinel: "not yet configured").
- `blockSize` default changed from `4096` → `0` (sentinel: "not yet configured").
- JSON load fallback values updated to match (`0`), so an existing `settings.json` that omits these fields is treated as "auto-detect" rather than silently using the old hard-coded values.

### `WasapiDeviceEnum.h` / `WasapiDeviceEnum.cpp`

- New method: `int getNativeSampleRate(const std::string& deviceId) const`  
  Activates an `IAudioClient` for the given endpoint (or the system default render endpoint when `deviceId` is empty), calls `GetMixFormat()`, and returns `nSamplesPerSec`. Returns `0` on any failure.
- Added `<audioclient.h>` include required by the new method.

### `MainWindow.cpp`

Immediately after `onDeviceListChanged()` resolves the device IDs on startup, two new lines fill in any zero values:

```cpp
if (settings_.sampleRate == 0) {
    int detected = deviceEnum_->getNativeSampleRate(settings_.outputDeviceId);
    settings_.sampleRate = (detected > 0) ? detected : 48000;
}
if (settings_.blockSize == 0)
    settings_.blockSize = 512;
```

The detected values are used for all subsequent `audioEngine_.start()` calls and are written to `settings.json` when the app exits normally, so the detection only happens once.

### `SettingsDialog.cpp`

- Default selected index for the block size combo changed from `4` (4096) to `1` (512) to match the new auto-detected default.
