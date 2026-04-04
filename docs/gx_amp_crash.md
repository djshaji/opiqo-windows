# Gx Amplifier crash on delete (Windows)

## Symptom

Deleting the *Gx Amp Stereo* plugin (`gx_amp_stereo.lv2`) crashed the process. The crash did **not** reproduce under WinDbg (debug heap) or on Wine/Linux. Log output before the crash:

```
[D] closePlugin: plugin already closed, skipping duplicate cleanup
[E] LV2HostWorker: thread still joinable in destructor — joining now
```

## Root causes

### 1. Uninitialized `closed_` atomic — `LV2Plugin.hpp`

`std::atomic<bool> closed_` had no initialiser. The C++17 default constructor for `atomic<bool>` is trivial and leaves the value **indeterminate**. On a Windows retail heap the backing memory is not zeroed, so `closed_` often started as non-zero. The very first call to `closePlugin()` then saw `was_closed == true`, skipped all cleanup (including `stop_worker()`), and returned early — leaving the worker thread running. The `~LV2HostWorker()` safety-net joined the thread without `lilv_instance_deactivate` ever having been called, causing the crash.

WinDbg uses a debug heap that zero-initialises allocations, masking the bug there.

**Fix:** `std::atomic<bool> closed_{false};`

### 2. `_aligned_malloc` / `free` mismatch for atom port buffers — `LV2Plugin.hpp`

Atom port buffers (`p.atom`) are allocated with `aligned_alloc(64, …)`, which maps to `_aligned_malloc` on MinGW/Windows. `closePlugin()` freed them with plain `free()`, corrupting the heap for the same reason as the earlier ringbuffer crash.

**Fix:** Use `aligned_free(p.atom)` (guarded by `#ifdef aligned_free` so Linux builds continue to use `free`).

### 3. `_aligned_malloc` / `free` mismatch for ringbuffers — `lv2_ringbuffer.h` (prior fix)

`lv2_ringbuffer_free()` freed `rb->buf` with `std::free`, but `rb->buf` was allocated via `_aligned_malloc`. This was the original crash for all other plugins.

**Fix:** Use `aligned_free(rb->buf)` in `lv2_ringbuffer_free()`.

## Files changed

| File | Change |
|------|--------|
| `src/LV2Plugin.hpp` | `closed_` initialised to `false`; `p.atom` freed with `aligned_free` |
| `src/lv2_ringbuffer.h` | `rb->buf` freed with `aligned_free` instead of `std::free` |
