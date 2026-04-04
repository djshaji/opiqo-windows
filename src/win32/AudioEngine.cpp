#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "AudioEngine.h"
#include "../LiveEffectEngine.h"

// Speex resampler (resample.c compiled with OUTSIDE_SPEEX + RANDOM_PREFIX=opiqo).
#define OUTSIDE_SPEEX
#define RANDOM_PREFIX opiqo
#include "../speex_resampler.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Pimpl
// ---------------------------------------------------------------------------

struct AudioEngine::Impl {
    // Configuration (captured at start()).
    int32_t     sampleRate   = 48000;
    int32_t     blockSize    = 4096;
    bool        exclusiveMode = false;

    // WASAPI objects.
    IMMDeviceEnumerator* enumerator     = nullptr;
    IAudioClient*        captureClient  = nullptr;
    IAudioClient*        renderClient   = nullptr;
    IAudioCaptureClient* captureService = nullptr;
    IAudioRenderClient*  renderService  = nullptr;

    // Negotiated format (owned by this struct).
    WAVEFORMATEX* captureFmt = nullptr;
    WAVEFORMATEX* renderFmt  = nullptr;

    // Synchronisation.
    HANDLE captureEvent = nullptr;
    HANDLE renderEvent  = nullptr;
    HANDLE stopEvent    = nullptr;

    // Pre-allocated scratch buffers (sized at start(), reused in loop).
    std::vector<float> inBuf;
    std::vector<float> outBuf;
    // Fixed stereo (2-ch) intermediate buffer passed to LiveEffectEngine::process().
    std::vector<float> stereoBuf;

    // DSP engine (non-owning pointer; nullptr = pass-through).
    LiveEffectEngine* engine = nullptr;

    // Optional capture→render sample-rate converter (nullptr when rates match).
    SpeexResamplerState* captureResampler = nullptr;
    // Resampled stereo input buffer (at renderFmt rate, 2-ch interleaved).
    std::vector<float> resampledBuf;
    // Stereo DSP output buffer (at renderFmt rate, 2-ch interleaved).
    std::vector<float> dspOutBuf;

    // Pending device IDs written by start() before the audio thread launches.
    std::string inDeviceId;
    std::string outDeviceId;

    // Thread and state.
    std::thread              thread;
    std::atomic<AudioEngine::State> state { AudioEngine::State::Off };
    mutable std::mutex       errorMsgMutex;   // Guards errorMsg across threads.
    std::string              errorMsg;
    std::atomic<uint64_t>    dropouts { 0 };
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

// Returns true when the format is IEEE float (WAVE_FORMAT_IEEE_FLOAT or
// WAVE_FORMAT_EXTENSIBLE with a float SubFormat).
static bool isFloatFormat(const WAVEFORMATEX* fmt) {
    if (!fmt) return false;
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
        // Compare SubFormat against the IEEE_FLOAT GUID without INITGUID:
        // {00000003-0000-0010-8000-00aa00389b71}
        static const GUID kFloat = {
            0x00000003, 0x0000, 0x0010,
            {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
        };
        return IsEqualGUID(ext->SubFormat, kFloat) != 0;
    }
    return false;
}

// Convert a WASAPI buffer to interleaved float.
// Handles IEEE float (direct copy) and 16-bit PCM (scaled).
static void toFloat(const BYTE* src, UINT32 frames, UINT32 channels,
                    const WAVEFORMATEX* fmt, float* dst) {
    if (isFloatFormat(fmt)) {
        std::memcpy(dst, src, frames * channels * sizeof(float));
        return;
    }
    UINT32 n = frames * channels;
    WORD bitsPerSample = fmt->wBitsPerSample;
    // For WAVE_FORMAT_EXTENSIBLE, wBitsPerSample reflects the container width.
    if (bitsPerSample == 32) {
        // 32-bit integer PCM.
        const int32_t* s = reinterpret_cast<const int32_t*>(src);
        for (UINT32 i = 0; i < n; ++i)
            dst[i] = static_cast<float>(s[i]) / 2147483648.f;
    } else if (bitsPerSample == 24) {
        // 24-bit packed PCM (3 bytes per sample, little-endian, sign-extended).
        for (UINT32 i = 0; i < n; ++i) {
            const BYTE* p = src + i * 3;
            int32_t v = (static_cast<int32_t>(p[0]))        |
                        (static_cast<int32_t>(p[1]) << 8)   |
                        (static_cast<int32_t>(p[2]) << 16);
            // Sign-extend from 24 bits.
            if (v & 0x800000) v |= static_cast<int32_t>(0xFF000000);
            dst[i] = static_cast<float>(v) / 8388608.f;
        }
    } else {
        // Fallback: 16-bit PCM.
        const int16_t* s = reinterpret_cast<const int16_t*>(src);
        for (UINT32 i = 0; i < n; ++i)
            dst[i] = static_cast<float>(s[i]) / 32768.f;
    }
}

// Write interleaved float to a WASAPI render buffer.
static void fromFloat(const float* src, UINT32 frames, UINT32 channels,
                      const WAVEFORMATEX* fmt, BYTE* dst) {
    if (isFloatFormat(fmt)) {
        std::memcpy(dst, src, frames * channels * sizeof(float));
        return;
    }
    UINT32 n = frames * channels;
    WORD bitsPerSample = fmt->wBitsPerSample;
    if (bitsPerSample == 32) {
        // 32-bit integer PCM.
        int32_t* d = reinterpret_cast<int32_t*>(dst);
        for (UINT32 i = 0; i < n; ++i) {
            float v = src[i];
            if (v >  1.f) v =  1.f;
            if (v < -1.f) v = -1.f;
            d[i] = static_cast<int32_t>(v * 2147483647.f);
        }
    } else if (bitsPerSample == 24) {
        // 24-bit packed PCM (3 bytes per sample, little-endian).
        for (UINT32 i = 0; i < n; ++i) {
            float v = src[i];
            if (v >  1.f) v =  1.f;
            if (v < -1.f) v = -1.f;
            int32_t s = static_cast<int32_t>(v * 8388607.f);
            BYTE* p = dst + i * 3;
            p[0] = static_cast<BYTE>( s        & 0xFF);
            p[1] = static_cast<BYTE>((s >>  8) & 0xFF);
            p[2] = static_cast<BYTE>((s >> 16) & 0xFF);
        }
    } else {
        // Fallback: 16-bit PCM.
        int16_t* d = reinterpret_cast<int16_t*>(dst);
        for (UINT32 i = 0; i < n; ++i) {
            float v = src[i];
            if (v >  1.f) v =  1.f;
            if (v < -1.f) v = -1.f;
            d[i] = static_cast<int16_t>(v * 32767.f);
        }
    }
}

// ---------------------------------------------------------------------------
// AudioEngine public API
// ---------------------------------------------------------------------------

AudioEngine::AudioEngine() : impl_(new Impl()) {}

AudioEngine::~AudioEngine() {
    stop();
    delete impl_;
}

void AudioEngine::setEngine(LiveEffectEngine* engine) {
    impl_->engine = engine;
}

AudioEngine::State AudioEngine::state() const {
    return impl_->state.load();
}

int32_t AudioEngine::sampleRate() const { return impl_->sampleRate; }
int32_t AudioEngine::blockSize()  const { return impl_->blockSize;  }

std::string AudioEngine::errorMessage() const {
    std::lock_guard<std::mutex> lk(impl_->errorMsgMutex);
    return impl_->errorMsg;
}

uint64_t AudioEngine::dropoutCount() const {
    return impl_->dropouts.load(std::memory_order_relaxed);
}

bool AudioEngine::start(int32_t sampleRate,
                        int32_t blockSize,
                        const std::string& inputDeviceId,
                        const std::string& outputDeviceId,
                        bool exclusiveMode) {
    AudioEngine::State expected = AudioEngine::State::Off;
    if (!impl_->state.compare_exchange_strong(expected, AudioEngine::State::Starting))
        return false;  // Already running or in transition.

    impl_->sampleRate    = sampleRate;
    impl_->blockSize     = blockSize;
    impl_->exclusiveMode = exclusiveMode;
    impl_->inDeviceId    = inputDeviceId;
    impl_->outDeviceId   = outputDeviceId;

    impl_->stopEvent = CreateEventW(nullptr, TRUE /*manual*/, FALSE, nullptr);
    if (!impl_->stopEvent) {
        impl_->state = AudioEngine::State::Error;
        impl_->errorMsg = "CreateEvent failed for stop signal";
        return false;
    }

    impl_->thread = std::thread([this]() { audioThreadProc(); });
    return true;
}

void AudioEngine::stop() {
    AudioEngine::State s = impl_->state.load();
    if (s == AudioEngine::State::Off || s == AudioEngine::State::Stopping)
        return;

    impl_->state = AudioEngine::State::Stopping;

    if (impl_->stopEvent)
        SetEvent(impl_->stopEvent);

    if (impl_->thread.joinable())
        impl_->thread.join();
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void AudioEngine::audioThreadProc() {
    // Read device IDs from dedicated fields (written by start() before thread launch).
    std::string inId  = impl_->inDeviceId;
    std::string outId = impl_->outDeviceId;

    // Helper: set errorMsg under its mutex. All writes from this thread go through here.
    auto setErr = [this](std::string msg) {
        std::lock_guard<std::mutex> lk(impl_->errorMsgMutex);
        impl_->errorMsg = std::move(msg);
    };

    // COM init for the audio thread — multithreaded apartment.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        setErr("Audio thread CoInitializeEx failed");
        impl_->state = AudioEngine::State::Error;
        return;
    }

    // -----------------------------------------------------------------------
    // Build IMMDeviceEnumerator
    // -----------------------------------------------------------------------
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                          IID_IMMDeviceEnumerator,
                          reinterpret_cast<void**>(&impl_->enumerator));
    if (FAILED(hr)) {
        setErr("CoCreateInstance(IMMDeviceEnumerator) failed");
        impl_->state = AudioEngine::State::Error;
        CoUninitialize();
        return;
    }

    auto activateClient = [&](const std::string& id, EDataFlow flow,
                               IAudioClient** out) -> bool {
        IMMDevice* dev = nullptr;
        HRESULT r;
        if (id.empty()) {
            r = impl_->enumerator->GetDefaultAudioEndpoint(flow, eConsole, &dev);
        } else {
            std::wstring wid = utf8ToWide(id);
            r = impl_->enumerator->GetDevice(wid.c_str(), &dev);
        }
        if (FAILED(r)) return false;
        r = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(out));
        dev->Release();
        return SUCCEEDED(r);
    };

    if (!activateClient(inId,  eCapture, &impl_->captureClient)
     || !activateClient(outId, eRender,  &impl_->renderClient)) {
        setErr("Failed to activate audio endpoints");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // -----------------------------------------------------------------------
    // Format negotiation — prefer IEEE float
    // -----------------------------------------------------------------------
    auto negotiateFormat = [&](IAudioClient* client,
                                WAVEFORMATEX** fmtOut) -> bool {
        WAVEFORMATEX* mix = nullptr;
        if (FAILED(client->GetMixFormat(&mix))) return false;
        *fmtOut = mix;
        return true;
    };

    if (!negotiateFormat(impl_->captureClient, &impl_->captureFmt)
     || !negotiateFormat(impl_->renderClient,  &impl_->renderFmt)) {
        setErr("GetMixFormat failed");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // Accept whatever rates the devices negotiated in shared mode — the OS
    // owns the mix format and may give different rates per endpoint.
    // Update sampleRate to the render device rate (authoritative for DSP).
    impl_->sampleRate = static_cast<int32_t>(impl_->renderFmt->nSamplesPerSec);

    // -----------------------------------------------------------------------
    // Initialise both clients in event-driven shared/exclusive mode
    // -----------------------------------------------------------------------
    AUDCLNT_SHAREMODE shareMode = impl_->exclusiveMode
        ? AUDCLNT_SHAREMODE_EXCLUSIVE
        : AUDCLNT_SHAREMODE_SHARED;

    // Exclusive mode requires a non-zero buffer duration equal to the device's
    // minimum period. Shared mode accepts 0 (let the driver decide).
    REFERENCE_TIME capturePeriod = 0;
    REFERENCE_TIME renderPeriod  = 0;
    if (impl_->exclusiveMode) {
        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        if (FAILED(impl_->captureClient->GetDevicePeriod(&defaultPeriod, &minPeriod))) {
            setErr("GetDevicePeriod failed for capture");
            impl_->state = AudioEngine::State::Error;
            releaseResources();
            return;
        }
        capturePeriod = minPeriod;

        defaultPeriod = 0; minPeriod = 0;
        if (FAILED(impl_->renderClient->GetDevicePeriod(&defaultPeriod, &minPeriod))) {
            setErr("GetDevicePeriod failed for render");
            impl_->state = AudioEngine::State::Error;
            releaseResources();
            return;
        }
        renderPeriod = minPeriod;
    }

    hr = impl_->captureClient->Initialize(
        shareMode,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        capturePeriod, impl_->exclusiveMode ? capturePeriod : 0,
        impl_->captureFmt, nullptr);
    if (FAILED(hr)) {
        setErr("IAudioClient::Initialize failed for capture");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    hr = impl_->renderClient->Initialize(
        shareMode,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        renderPeriod, impl_->exclusiveMode ? renderPeriod : 0,
        impl_->renderFmt, nullptr);
    if (FAILED(hr)) {
        setErr("IAudioClient::Initialize failed for render");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // Separate auto-reset events for capture and render.
    impl_->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->captureEvent) {
        setErr("CreateEvent failed for capture");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    impl_->renderEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->renderEvent) {
        setErr("CreateEvent failed for render");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    impl_->captureClient->SetEventHandle(impl_->captureEvent);
    impl_->renderClient->SetEventHandle(impl_->renderEvent);

    // -----------------------------------------------------------------------
    // Get capture and render services
    // -----------------------------------------------------------------------
    hr = impl_->captureClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&impl_->captureService));
    if (FAILED(hr)) {
        setErr("GetService(IAudioCaptureClient) failed");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    hr = impl_->renderClient->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&impl_->renderService));
    if (FAILED(hr)) {
        setErr("GetService(IAudioRenderClient) failed");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // -----------------------------------------------------------------------
    // Pre-roll render buffer with silence
    // -----------------------------------------------------------------------
    UINT32 renderBufSize = 0;
    impl_->renderClient->GetBufferSize(&renderBufSize);
    {
        BYTE* silenceData = nullptr;
        if (SUCCEEDED(impl_->renderService->GetBuffer(renderBufSize, &silenceData)))
            impl_->renderService->ReleaseBuffer(renderBufSize,
                                                AUDCLNT_BUFFERFLAGS_SILENT);
    }

    // -----------------------------------------------------------------------
    // Allocate scratch buffers
    // -----------------------------------------------------------------------
    // Use each device's own buffer size so that capture packets (which are
    // sized by the capture device period, not the render device period) can
    // never overflow inBuf on systems with mismatched capture/render periods.
    UINT32 captureBufSize = 0;
    impl_->captureClient->GetBufferSize(&captureBufSize);

    UINT32 capChannels = impl_->captureFmt->nChannels;
    UINT32 renChannels = impl_->renderFmt->nChannels;
    UINT32 maxBufSize  = std::max(captureBufSize, renderBufSize);
    UINT32 maxChannels = std::max(capChannels, renChannels);

    impl_->inBuf.assign(captureBufSize * capChannels, 0.f);
    impl_->outBuf.assign(maxBufSize    * renChannels, 0.f);
    // Stereo (2-ch) downmix buffer at capture rate — input side before DSP.
    impl_->stereoBuf.assign(captureBufSize * 2, 0.f);
    // DSP output buffer at render rate (2-ch interleaved).
    impl_->dspOutBuf.assign(std::max(captureBufSize, renderBufSize) * 2, 0.f);
    (void)maxChannels;

    // Create a resampler if the capture and render devices run at different rates.
    if (impl_->captureFmt->nSamplesPerSec != impl_->renderFmt->nSamplesPerSec) {
        int rsErr = 0;
        impl_->captureResampler = speex_resampler_init(
            2,
            impl_->captureFmt->nSamplesPerSec,
            impl_->renderFmt->nSamplesPerSec,
            SPEEX_RESAMPLER_QUALITY_DESKTOP,
            &rsErr);
        if (!impl_->captureResampler || rsErr != RESAMPLER_ERR_SUCCESS) {
            setErr("speex_resampler_init failed (err " + std::to_string(rsErr) + ")");
            impl_->state = AudioEngine::State::Error;
            releaseResources();
            return;
        }
        speex_resampler_skip_zeros(impl_->captureResampler);
        // Max output frames per capture packet, with a small margin for
        // resampler start-up latency.
        UINT32 maxOut = captureBufSize
            * impl_->renderFmt->nSamplesPerSec
            / impl_->captureFmt->nSamplesPerSec + 128;
        impl_->resampledBuf.assign(maxOut * 2, 0.f);
    }

    // -----------------------------------------------------------------------
    // Start streams
    // -----------------------------------------------------------------------
    impl_->renderClient->Start();
    impl_->captureClient->Start();

    // Boost thread priority via MMCSS.
    DWORD taskIndex = 0;
    HANDLE mmHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    // Reset dropout counter for this session.
    impl_->dropouts.store(0, std::memory_order_relaxed);

    impl_->state = AudioEngine::State::Running;

    // -----------------------------------------------------------------------
    // Run the pass-through loop
    // -----------------------------------------------------------------------
    bool deviceLost = !runLoop();

    // -----------------------------------------------------------------------
    // Teardown
    // -----------------------------------------------------------------------
    impl_->captureClient->Stop();
    impl_->renderClient->Stop();

    if (mmHandle) AvRevertMmThreadCharacteristics(mmHandle);

    releaseResources();
    CoUninitialize();

    impl_->state = deviceLost ? AudioEngine::State::Error
                              : AudioEngine::State::Off;
}

bool AudioEngine::runLoop() {
    HANDLE events[2] = { impl_->captureEvent, impl_->stopEvent };

    while (true) {
        DWORD wait = WaitForMultipleObjects(2, events, FALSE, 200 /*ms timeout*/);

        if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_TIMEOUT) {
            // Stop event or timeout — check if stopping was requested.
            if (impl_->state.load() == AudioEngine::State::Stopping)
                return true;
            if (wait == WAIT_OBJECT_0 + 1)
                return true;  // Stop event.
            continue;
        }

        if (wait != WAIT_OBJECT_0)
            return false;  // Unexpected error.

        // Process all available capture packets.
        UINT32 packetFrames = 0;
        while (SUCCEEDED(impl_->captureService->GetNextPacketSize(&packetFrames))
               && packetFrames > 0) {
            BYTE*  captureData  = nullptr;
            UINT32 numFrames    = 0;
            DWORD  captureFlags = 0;

            HRESULT hr = impl_->captureService->GetBuffer(
                &captureData, &numFrames, &captureFlags, nullptr, nullptr);
            if (FAILED(hr)) return false;

            UINT32 capChannels = impl_->captureFmt->nChannels;
            UINT32 renChannels = impl_->renderFmt->nChannels;

            bool silent = (captureFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            if (captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                impl_->dropouts.fetch_add(1, std::memory_order_relaxed);

            if (silent) {
                std::fill(impl_->inBuf.begin(),
                          impl_->inBuf.begin() + numFrames * capChannels, 0.f);
            } else {
                toFloat(captureData, numFrames, capChannels,
                        impl_->captureFmt, impl_->inBuf.data());
            }

            impl_->captureService->ReleaseBuffer(numFrames);

            // Always downmix capture to stereo (2-ch interleaved, capture rate).
            for (UINT32 f = 0; f < numFrames; ++f) {
                impl_->stereoBuf[f * 2 + 0] =
                    (capChannels >= 1) ? impl_->inBuf[f * capChannels + 0] : 0.f;
                impl_->stereoBuf[f * 2 + 1] =
                    (capChannels >= 2) ? impl_->inBuf[f * capChannels + 1]
                                      : impl_->stereoBuf[f * 2];
            }

            // Optionally resample capture data to the render device's rate.
            const float* dspIn = impl_->stereoBuf.data();
            UINT32 dspFrames = numFrames;
            if (impl_->captureResampler) {
                spx_uint32_t inLen  = static_cast<spx_uint32_t>(numFrames);
                spx_uint32_t outLen = static_cast<spx_uint32_t>(
                    impl_->resampledBuf.size() / 2);
                speex_resampler_process_interleaved_float(
                    impl_->captureResampler,
                    impl_->stereoBuf.data(), &inLen,
                    impl_->resampledBuf.data(), &outLen);
                dspIn     = impl_->resampledBuf.data();
                dspFrames = static_cast<UINT32>(outLen);
            }

            // DSP or pass-through — stereo in, stereo out at render rate.
            if (impl_->engine) {
                impl_->engine->process(const_cast<float*>(dspIn), impl_->dspOutBuf.data(),
                                       static_cast<int>(dspFrames));
            } else {
                std::memcpy(impl_->dspOutBuf.data(), dspIn,
                            dspFrames * 2 * sizeof(float));
            }

            // Upmix stereo output to render channel count.
            for (UINT32 f = 0; f < dspFrames; ++f) {
                for (UINT32 c = 0; c < renChannels; ++c) {
                    impl_->outBuf[f * renChannels + c] =
                        (c < 2) ? impl_->dspOutBuf[f * 2 + c] : 0.f;
                }
            }

            // Write to render buffer.
            // If the render buffer is fuller than expected (e.g. Wine/WASAPI
            // emulation timing jitter), write only what fits this period and
            // carry any excess frames into the next callback via silence padding
            // rather than discarding them — this avoids render-side underruns.
            UINT32 padding = 0;
            impl_->renderClient->GetCurrentPadding(&padding);
            UINT32 renderBufSize = 0;
            impl_->renderClient->GetBufferSize(&renderBufSize);
            UINT32 available = renderBufSize - padding;

            if (available == 0) {
                // Render buffer is full — count as a dropout and skip.
                impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            UINT32 toWrite = std::min(dspFrames, available);

            BYTE* renderData = nullptr;
            hr = impl_->renderService->GetBuffer(toWrite, &renderData);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) return false;
                continue;
            }

            fromFloat(impl_->outBuf.data(), toWrite, renChannels,
                      impl_->renderFmt, renderData);
            impl_->renderService->ReleaseBuffer(toWrite, 0);

            // If the render device accepted fewer frames than we processed,
            // pad the render buffer with silence for the remainder so the
            // next period starts with a full buffer.
            if (toWrite < dspFrames) {
                UINT32 gap = dspFrames - toWrite;
                UINT32 gapAvail = renderBufSize - (padding + toWrite);
                UINT32 silFrames = std::min(gap, gapAvail);
                if (silFrames > 0) {
                    BYTE* silData = nullptr;
                    if (SUCCEEDED(impl_->renderService->GetBuffer(silFrames, &silData)))
                        impl_->renderService->ReleaseBuffer(silFrames,
                                                            AUDCLNT_BUFFERFLAGS_SILENT);
                }
                impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

void AudioEngine::releaseResources() {
    if (impl_->captureResampler) {
        speex_resampler_destroy(impl_->captureResampler);
        impl_->captureResampler = nullptr;
    }
    if (impl_->captureService) { impl_->captureService->Release(); impl_->captureService = nullptr; }
    if (impl_->renderService)  { impl_->renderService->Release();  impl_->renderService  = nullptr; }
    if (impl_->captureClient)  { impl_->captureClient->Release();  impl_->captureClient  = nullptr; }
    if (impl_->renderClient)   { impl_->renderClient->Release();   impl_->renderClient   = nullptr; }
    if (impl_->enumerator)     { impl_->enumerator->Release();     impl_->enumerator     = nullptr; }
    if (impl_->captureFmt)     { CoTaskMemFree(impl_->captureFmt); impl_->captureFmt     = nullptr; }
    if (impl_->renderFmt)      { CoTaskMemFree(impl_->renderFmt);  impl_->renderFmt      = nullptr; }
    if (impl_->captureEvent)   { CloseHandle(impl_->captureEvent); impl_->captureEvent   = nullptr; }
    if (impl_->renderEvent)    { CloseHandle(impl_->renderEvent);  impl_->renderEvent    = nullptr; }
    if (impl_->stopEvent)      { CloseHandle(impl_->stopEvent);    impl_->stopEvent      = nullptr; }
}

