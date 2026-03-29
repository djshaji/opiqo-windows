# WASAPI Audio Implementation Details

## Goal

Implement low-latency duplex audio on Windows using WASAPI with this processing chain:

Audio input -> `LiveEffectEngine::process(float* input, float* output, int frames)` -> Audio output

The audio callback/thread must never block on UI work.

---

## Module Boundaries

- `AudioEngine` owns all WASAPI COM objects and the audio thread.
- `LiveEffectEngine` owns LV2 graph processing and recording state.
- Win32 UI selects devices/mode/sample rate/block size and requests engine restart.

---

## Power Toggle Contract

- The Power control in UI is a true toggle for transport state (audio engine On/Off).
- Power On:
	- Read currently selected `inputDeviceId` and `outputDeviceId` from UI.
	- Call `AudioEngine::start(sampleRate, blockSize, inputDeviceId, outputDeviceId, shareMode)`.
	- If start succeeds, keep toggle On and show running state in status bar.
	- If start fails, force toggle back Off and show error.
- Power Off:
	- Call `AudioEngine::stop()`.
	- Keep selected device IDs unchanged so next Power On uses the same endpoints.

### Power State Machine

| State | Event | Action | Next State |
|---|---|---|---|
| `Off` | Power toggled On | Validate selected device IDs, call `AudioEngine::start(...)` | `Starting` |
| `Starting` | Start success | Update status bar (`Running`, device names, sample rate/block size) | `Running` |
| `Starting` | Start failure | Show error, force Power toggle Off | `Error` |
| `Running` | Power toggled Off | Call `AudioEngine::stop()` | `Stopping` |
| `Stopping` | Stop complete | Keep device selections persisted | `Off` |
| `Running` | Device changed (UI) | Stop + restart with new endpoint IDs | `Starting` |
| `Running` | `AUDCLNT_E_DEVICE_INVALIDATED` | Post `WM_APP_DEVICE_LOST`, stop stream, refresh devices | `Error` |
| `Error` | User acknowledges / retries | Clear error; if Power On requested, retry start | `Off` or `Starting` |

Implementation notes:

- Keep a single atomic state variable (`Off`, `Starting`, `Running`, `Stopping`, `Error`) owned by `AudioEngine`.
- Ignore duplicate transitions (for example Power On while already `Running`).
- UI toggle reflects effective state, not requested state (only stays On after start success).

---

## Data Flow

```text
Capture endpoint (WASAPI)
	-> deinterleave/convert if needed
	-> input float buffer [frames * channels]
	-> LiveEffectEngine::process(input, output, frames)
	-> interleave/convert if needed
	-> Render endpoint (WASAPI)
```

If capture and render formats are both IEEE float 32-bit stereo, avoid extra conversion and run direct float buffers.

---

## Initialization Sequence

1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`.
2. Create `IMMDeviceEnumerator`.
3. Resolve capture device (`eCapture`) and render device (`eRender`) by ID from UI selection.
4. Activate `IAudioClient` for capture and render.
5. Negotiate format:
	 - Shared mode: call `GetMixFormat`, prefer float.
	 - Exclusive mode: request desired format; fall back to shared if unsupported.
6. Set event-driven mode (`AUDCLNT_STREAMFLAGS_EVENTCALLBACK`) and create event handles.
7. Initialize clients with matched frame period.
8. Get `IAudioCaptureClient` and `IAudioRenderClient`.
9. Pre-roll render buffer with silence.
10. Start render then capture clients.
11. Launch audio thread loop.

---

## Audio Thread Loop

Pseudo-flow:

```cpp
while (running) {
		WaitForSingleObject(captureEvent, timeoutMs);

		// 1) Read all available capture packets
		while (captureClient->GetNextPacketSize(&packetFrames) == S_OK && packetFrames > 0) {
				captureClient->GetBuffer(&inData, &numFrames, &flags, nullptr, nullptr);

				// Convert input to float interleaved if required
				convertCaptureToFloat(inData, numFrames, inFloatBuffer);

				// 2) Process through LV2 chain
				liveEffectEngine->process(inFloatBuffer.data(), outFloatBuffer.data(), numFrames);

				captureClient->ReleaseBuffer(numFrames);

				// 3) Acquire render space and write processed frames
				renderClient->GetBuffer(numFrames, &outData);
				convertFloatToRender(outFloatBuffer, numFrames, outData);
				renderClient->ReleaseBuffer(numFrames, 0);
		}
}
```

Rules:

- No heap allocation in loop.
- No file I/O in loop.
- No mutex lock that can block on UI path.
- Reuse preallocated vectors for input/output/convert scratch buffers.

---

## Buffering and Latency

- Let `bufferFrames` be negotiated by `IAudioClient`.
- Engine call uses `frames == numFrames` provided by capture packet.
- Target end-to-end latency in shared mode: roughly one to two device periods.
- If capture and render period differ, use a small ring buffer bridge and consume at render rate.

---

## Device Selection Contract

`AudioEngine::start(...)` parameters should include:

- `sampleRate`
- `blockSize` (preferred; actual device period may differ)
- `inputDeviceId` (WASAPI endpoint ID string)
- `outputDeviceId` (WASAPI endpoint ID string)
- `shareMode` (`AUDCLNT_SHAREMODE_SHARED` or `AUDCLNT_SHAREMODE_EXCLUSIVE`)

When device selection changes in UI:

1. If Power is Off: store new endpoint IDs only (defer start).
2. If Power is On: call `AudioEngine::stop()`, recreate clients with new endpoint IDs, then call `AudioEngine::start(...)`.
3. Post status message on success/failure.

---

## Failure Handling

- `AUDCLNT_E_DEVICE_INVALIDATED`: device unplugged/default changed.
	- Post `WM_APP_DEVICE_LOST` to UI.
	- Stop engine, refresh device list, prompt user, restart with selected/default device.
- `AUDCLNT_E_UNSUPPORTED_FORMAT`:
	- Retry with mix format.
	- If still failing, show blocking error in status area.
- Capture underflow/render starvation:
	- Fill render with silence for that period.
	- Keep thread alive and log rate-limited warning.

---

## Threading and Synchronization

- WASAPI thread priority: set via MMCSS (`Pro Audio` profile).
- UI-to-audio updates:
	- plugin parameter writes through lock-free/atomic path,
	- heavy operations (plugin add/delete, device restart) done outside real-time loop.
- `LiveEffectEngine::process(...)` must be called from audio thread only.

---

## Validation Checklist

- Pass-through test (no plugin): input is audible at output.
- Plugin test: each slot affects stream in order.
- Device switch test while running.
- Shared/exclusive mode smoke tests.
- 44.1k/48k/96k sample rate tests.
- Recording still works during live processing.
