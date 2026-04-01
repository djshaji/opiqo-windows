# Milestone 8 — Recording UX and Format Handling

## What the milestone requires

- Record button start/stop behavior with label change ("● Record" → "■ Stop")
- Bind format dropdown (WAV/MP3/OGG) to `startRecording(fd, fileType, quality)`
- Show/hide a quality selector for lossy formats (MP3/OGG)
- Connect format and quality to `startRecording` arguments

---

## What already exists (usable as-is)

| Component | Status |
|---|---|
| `ControlBar` | Record toggle button (`IDC_RECORD_TOGGLE`, `BS_AUTOCHECKBOX | BS_PUSHLIKE`) and format combobox (`IDC_FORMAT_COMBO`, WAV/MP3/OGG, index 0/1/2) already created and functional. `setRecordState(bool)`, `formatIndex()`, `setFormatIndex(int)` all implemented. |
| `AppSettings` | `recordFormat` and `recordQuality` fields present, persisted to JSON on shutdown and restored on startup. |
| `LiveEffectEngine` | `startRecording(int fd, int file_type, int quality)` and `stopRecording()` fully implemented. `fileWriter->open()` routes to libsndfile (WAV/OGG/FLAC), liblame (MP3), libopusenc (Opus), or libvorbis (OGG fallback). `FileWriter::recording` flag gates the encode callback. |
| `FileWriter` | `encode(AudioBuffer*)` called automatically by the `LockFreeQueue` processing path (`process()`) — recording happens concurrently with live DSP without UI involvement. `close()` flushes and finalises all encoder states. |
| Quality mapping | `openSndfile()` maps quality `0→1.0f`, `1→0.75f`, `2→0.5f` and applies `SFC_SET_VBR_ENCODING_QUALITY` to libsndfile. |

---

## What was implemented

### `src/win32/resource.h`
- Added `IDC_QUALITY_COMBO 50104`.

### `src/win32/ControlBar.h`
- Added `qualityCombo_` private member (`HWND`).
- Added `qualityIndex()` getter declaration.
- Added `showQualityCombo(bool)` declaration.

### `src/win32/ControlBar.cpp`
- Quality combo (`IDC_QUALITY_COMBO`) created in `create()` at x=416, w=90, with entries High / Medium / High (index 0/1/2). Created without `WS_VISIBLE` — hidden by default (WAV is the startup format).
- `setRecordState(bool)` updated to call `SetWindowTextA` on the record button: `"■ Stop"` when recording, `"● Record"` when stopped.
- `qualityIndex()` implemented: reads `CB_GETCURSEL` from `qualityCombo_`, returns 0 on error.
- `showQualityCombo(bool)` implemented: calls `ShowWindow(qualityCombo_, SW_SHOW / SW_HIDE)`.

### `src/win32/MainWindow.h`
- Added `int recordingFd_ = -1` to track the open file descriptor across the recording session.

### `src/win32/MainWindow.cpp`
- Added `#include <fcntl.h>`, `#include <io.h>`, and `#include "../FileWriter.h"` for `_open`/`_close` and the `FileType` enum.
- `IDC_RECORD_TOGGLE` case added to the `WM_COMMAND` switch:
  - **Start**: builds a per-format `OPENFILENAMEA` filter (`"WAV Files\0*.wav\0…"` etc.), calls `GetSaveFileNameA`, opens the file with `_open(_O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY)`, calls `liveEngine_.startRecording(fd, formatMap[fmtIdx], qualityIndex)`. On any failure the button is reverted and a `MessageBoxA` error is shown.
  - **Stop**: calls `liveEngine_.stopRecording()` first (so all encoder flush writes complete), then `_close(recordingFd_)`, resets `recordingFd_ = -1`, calls `controlBar_.setRecordState(false)`.
  - `settings_.recordFormat` and `settings_.recordQuality` updated on successful start.
- `IDC_FORMAT_COMBO` case added to the `WM_COMMAND` switch:
  - On `CBN_SELCHANGE`: reads `controlBar_.formatIndex()`, writes to `settings_.recordFormat`, calls `controlBar_.showQualityCombo(fmtIdx != 0)`.
- Static `formatMap[]` array: `{ FILE_TYPE_WAV, FILE_TYPE_MP3, FILE_TYPE_OGG }` — explicit mapping to avoid the combobox-index ≠ enum-value pitfall (OGG is index 2 but `FILE_TYPE_OGG` is 4).
- Startup: after `controlBar_.create()`, `setFormatIndex(settings_.recordFormat)` and `showQualityCombo(settings_.recordFormat != 0)` are called to restore persisted state.

---

## Decisions and notes

| Topic | Decision |
|---|---|
| `_open` vs `open` | Used `_open` / `_close` from `<io.h>` — the MinGW POSIX aliases work but `_open` is the canonical CRT name on Win32 |
| fd lifetime | `stopRecording()` called before `_close()` in all paths — ensures `lame_encode_flush` and libsndfile finalisation complete before the fd is invalidated |
| Quality index mapping | High=0, Medium=1, Low=2 — matches the `openSndfile()` quality float mapping (0→1.0f, 1→0.75f, 2→0.5f) |
| Quality combo visibility | Hidden at creation; shown only when MP3 or OGG is selected; WAV has no meaningful quality knob |
| OGG vs OPUS | The combobox label "OGG" maps to `FILE_TYPE_OGG` (libvorbis), not `FILE_TYPE_OPUS`. This matches user expectation for a plain `.ogg` file. |
