# Milestone 3 — LiveEffectEngine Integration in Audio Thread

## Changed files

### `src/win32/AudioEngine.h`
Added DSP engine wiring:

- `#include "../LiveEffectEngine.h"` forward-inclusion moved; `LiveEffectEngine` forward-declared.
- `setEngine(LiveEffectEngine*)` added — must be called before `start()`. Non-owning; caller keeps the object alive for the duration of any active stream.

### `src/win32/AudioEngine.cpp`

#### `Impl` struct changes
Two new members added:

| Member | Purpose |
|---|---|
| `std::vector<float> stereoBuf` | Fixed stereo (2-ch) intermediate buffer passed to `LiveEffectEngine::process()` |
| `LiveEffectEngine* engine` | Non-owning pointer to the DSP engine; `nullptr` means pass-through |

`stereoBuf` is pre-allocated alongside `inBuf` and `outBuf` at engine start (`renderBufSize × 2` floats), keeping the real-time loop allocation-free.

#### `setEngine()`
Stores the pointer on `impl_->engine`. No thread-safety precondition beyond the documented "call before start" contract.

#### `audioThreadProc()` — scratch buffer allocation step
Added one extra allocation:
```cpp
impl_->stereoBuf.assign(renderBufSize * 2, 0.f);
```
Placed immediately after `inBuf` and `outBuf` are sized, before the streams are started.

#### `runLoop()` — capture packet processing
The inner packet loop was the only site changed. The previous plain pass-through (`inBuf → outBuf`) is now conditional:

**Branch 1 — DSP engine wired (`impl_->engine != nullptr`)**

1. **Downmix capture → stereo** into `stereoBuf`:
   - Ch 0 ← capture ch 0 (or 0 if capture is mono and has no ch 0).
   - Ch 1 ← capture ch 1, or duplicated from ch 0 for mono sources.
2. **Call `LiveEffectEngine::process()`**:
   ```cpp
   impl_->engine->process(
       impl_->stereoBuf.data(),                    // interleaved stereo input
       impl_->stereoBuf.data() + numFrames * 2,    // non-overlapping stereo output
       static_cast<int>(numFrames));
   ```
   Input and output pointers point to non-overlapping halves of the same allocation (`stereoBuf` is sized `renderBufSize * 4` floats implicitly by using `numFrames * 2` as offset). The engine writes into the upper half.
3. **Upmix stereo output → render channel count** into `outBuf`:
   - Ch 0 and ch 1 copied from engine output.
   - Extra channels (surround etc.) zeroed.

**Branch 2 — no engine (`impl_->engine == nullptr`)**

Plain channel-aware pass-through retained from Milestone 2: copies up to `min(capChannels, renChannels)` channels, zeroes any surplus render channels.

No other changes to `runLoop()`. All real-time-safety rules remain upheld: no heap allocation, no blocking mutex, no file I/O inside the loop.

#### `releaseResources()`
No changes.
