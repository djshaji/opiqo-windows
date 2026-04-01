# Milestone 2 — AudioEngine Core (No DSP Yet)

## Changed files

### `src/win32/AudioEngine.h`
Replaced the no-op stub with a full class definition:

- `State` enum promoted to use `std::atomic<State>` internally (via pimpl).
- Constructor and destructor declared (pimpl lifetime management).
- Copy and assignment deleted.
- `start()` / `stop()` signatures unchanged; now have real semantics.
- Added read-only accessors: `sampleRate()`, `blockSize()`, `errorMessage()`.
- Private interface split into: `audioThreadProc()`, `runLoop()`, `releaseResources()`, and `Impl*` pimpl.

### `src/win32/AudioEngine.cpp`
Full WASAPI duplex pipeline replacing the three-line stub.

#### `Impl` struct (pimpl)
Holds all mutable state:

| Member | Purpose |
|---|---|
| `sampleRate`, `blockSize`, `exclusiveMode` | Configuration captured at `start()` |
| `IMMDeviceEnumerator*` | COM enumerator used to resolve device IDs |
| `IAudioClient*` × 2 | Capture and render stream clients |
| `IAudioCaptureClient*`, `IAudioRenderClient*` | Per-packet read/write services |
| `WAVEFORMATEX*` × 2 | Negotiated formats (freed with `CoTaskMemFree`) |
| `HANDLE captureEvent` | Auto-reset event signalled by WASAPI each period |
| `HANDLE stopEvent` | Manual-reset event; set by `stop()` to terminate loop |
| `std::vector<float>` × 2 | Pre-allocated scratch buffers — no heap allocation in loop |
| `std::thread` | Audio thread |
| `std::atomic<State>` | Engine state, thread-safe |
| `std::string errorMsg` | Human-readable failure reason; also used as a device-ID carrier before the thread starts |

#### `start()`
1. `compare_exchange_strong(Off → Starting)` — ignores duplicate calls.
2. Stores `sampleRate`, `blockSize`, `exclusiveMode`, and device IDs (serialised as `"inputId|outputId"` in `errorMsg`).
3. Creates the manual-reset stop event.
4. Launches `audioThreadProc()` on a `std::thread`.

#### `audioThreadProc()`
Runs entirely on the audio thread:

1. Extracts device IDs from `errorMsg` carrier and clears it.
2. `CoInitializeEx(COINIT_MULTITHREADED)`.
3. `CoCreateInstance(CLSID_MMDeviceEnumerator)`.
4. Activates `IAudioClient` for capture (`eCapture`) and render (`eRender`) by endpoint ID; falls back to `GetDefaultAudioEndpoint` when the ID is empty.
5. `GetMixFormat()` on each client → stores `captureFmt` and `renderFmt`.
6. `IAudioClient::Initialize(shareMode, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, ...)` on both clients.
7. Creates a single auto-reset capture event; sets it on both clients (shared event-driven model).
8. `GetService(IAudioCaptureClient)` and `GetService(IAudioRenderClient)`.
9. Pre-rolls the render buffer with silence.
10. Allocates `inBuf` and `outBuf` sized to `renderBufSize × maxChannels` — the only heap allocation before the loop.
11. `renderClient->Start()` then `captureClient->Start()`.
12. `AvSetMmThreadCharacteristicsW(L"Pro Audio")` for MMCSS priority boost.
13. Sets state to `Running` and calls `runLoop()`.
14. After loop exit: stops both clients, reverts MMCSS, calls `releaseResources()`, `CoUninitialize()`.
15. Sets final state to `Error` (device lost) or `Off` (clean stop).

On any failure before the loop, sets `errorMsg` and transitions to `Error` state.

#### `runLoop()` — pass-through loop
```
while (true):
    WaitForMultipleObjects([captureEvent, stopEvent], timeout=200ms)
    → stop event / Stopping state → return true (clean)
    → capture event → drain all capture packets:
        captureService->GetNextPacketSize(...)
        captureService->GetBuffer(...)
        toFloat(captureData → inBuf)
        captureService->ReleaseBuffer(...)
        copy inBuf → outBuf  (pass-through; handles channel count mismatch)
        renderService->GetBuffer(available frames)
        fromFloat(outBuf → renderData)
        renderService->ReleaseBuffer(...)
    → AUDCLNT_E_DEVICE_INVALIDATED → return false (device lost)
```

Rules enforced: no heap allocation, no blocking mutex, no file I/O inside loop.

#### `releaseResources()`
Releases all COM interfaces and closes both HANDLEs in reverse-activation order:
`captureService` → `renderService` → `captureClient` → `renderClient` → `enumerator` → `CoTaskMemFree(captureFmt)` → `CoTaskMemFree(renderFmt)` → `CloseHandle(captureEvent)` → `CloseHandle(stopEvent)`.

#### `stop()`
Checks state; if already `Off` or `Stopping`, returns immediately. Otherwise sets state to `Stopping`, signals `stopEvent`, and joins the thread.

#### Format helpers
`isFloatFormat()` checks for `WAVE_FORMAT_IEEE_FLOAT` or `WAVE_FORMAT_EXTENSIBLE` with the IEEE float sub-format GUID defined inline — avoids the MinGW-unresolved `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT` symbol.

`toFloat()` / `fromFloat()` convert between WASAPI byte buffers and interleaved `float` arrays; support float (memcpy) and 16-bit PCM (scaled integer) paths.

### `src/win32/MainWindow.h`
- Added `#include "AudioEngine.h"`.
- Added `AudioEngine audioEngine_` member.

### `src/win32/MainWindow.cpp`
- `WM_DESTROY` handler calls `audioEngine_.stop()` before `deviceEnum_.reset()` and `CoUninitialize()`.

### `CMakeLists.txt`
- Added `avrt` to `target_link_libraries` (required for `AvSetMmThreadCharacteristicsW` / `AvRevertMmThreadCharacteristics`).

---

## Build fix: MinGW GUID resolution
`KSDATAFORMAT_SUBTYPE_IEEE_FLOAT` is not exported as a linkable symbol by MinGW. The fix replaces the GUID comparison with a locally-defined static `GUID kFloat` (matching the IEEE float sub-format bytes) and uses `IsEqualGUID()`. This avoids both `#define INITGUID` and `-lksuser`.

---

## Acceptance criteria status

| Criterion | Status |
|---|---|
| Full duplex WASAPI pipeline starts without crash | Met — `CoInitialize` → device activation → format negotiation → event-driven stream |
| Pass-through path (no DSP) | Met — `inBuf` copied to `outBuf` frame-by-frame; channel count mismatch handled |
| No heap allocation in audio loop | Met — scratch buffers pre-allocated in `audioThreadProc()` before loop |
| Stop/start safe from UI thread | Met — stop event + thread join; `compare_exchange_strong` guards duplicate calls |
| Device loss handled | Met — `AUDCLNT_E_DEVICE_INVALIDATED` terminates loop and sets `State::Error` |
| MMCSS priority boost | Met — `AvSetMmThreadCharacteristicsW(L"Pro Audio")` on audio thread |
| Clean MinGW cross-compile | Met — `make` exits 0 with no warnings |
