#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "AudioEngine.h"
#include "../LiveEffectEngine.h"
#include "../lv2_ringbuffer.h"

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
    // IAudioClient3 (Windows 10 1607+): non-null when QueryInterface succeeds.
    // Shares the same COM object as captureClient/renderClient (each AddRef'd).
    IAudioClient3*       captureClient3 = nullptr;
    IAudioClient3*       renderClient3  = nullptr;
    IAudioCaptureClient* captureService = nullptr;
    IAudioRenderClient*  renderService  = nullptr;

    // Negotiated format (owned by this struct).
    WAVEFORMATEX* captureFmt = nullptr;
    WAVEFORMATEX* renderFmt  = nullptr;

    // Synchronisation.
    HANDLE captureEvent = nullptr;
    // renderEvent intentionally absent: render writes are capture-driven
    // (polling), so event-driven mode on the render client is not used.
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

    // Lock-free ring buffer between DSP output and render write.
    // Holds render-channel-count float samples (same layout as outBuf).
    // Prevents processed frames from being discarded when the render
    // client is temporarily full due to timing jitter.
    lv2_ringbuffer_t* renderDrainRing = nullptr;

    // Pending device IDs written by start() before the audio thread launches.
    std::string inDeviceId;
    std::string outDeviceId;

    // Thread and state.
    std::thread              thread;
    std::atomic<AudioEngine::State> state { AudioEngine::State::Off };
    mutable std::mutex       errorMsgMutex;   // Guards errorMsg across threads.
    std::string              errorMsg;
    std::atomic<uint64_t>    dropouts { 0 };

    // Combined stream latency (capture + render) in milliseconds.
    // Written once by the audio thread after Initialize(); read-only thereafter.
    std::atomic<double>      streamLatencyMs { 0.0 };

    // Drift correction state.
    // We accumulate render buffer fill (GetCurrentPadding) over a window of
    // capture callbacks and periodically compare to the target fill.
    // Over-fill → discard frames; under-fill → inject silence.
    uint32_t driftPackets   = 0;    // packets seen in current window
    uint64_t driftFillSum   = 0;    // sum of padding samples in window
    uint32_t driftTarget    = 0;    // target fill in frames (set at start)
    uint32_t driftPeriodFr  = 0;    // correction granularity in frames
    static constexpr uint32_t kDriftWindow = 50; // packets between corrections
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

double AudioEngine::streamLatencyMs() const {
    return impl_->streamLatencyMs.load(std::memory_order_relaxed);
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

    // Activate an IAudioClient endpoint, optionally upgrading to IAudioClient3.
    // In shared mode, tries Activate(IID_IAudioClient3) first (Windows 10 1607+).
    // On success, *out points to the IAudioClient3 (valid as IAudioClient*) and
    // *out3 holds the same pointer with an extra AddRef so each can be Released
    // independently in releaseResources().  On failure, falls back to IAudioClient.
    auto activateClient = [&](const std::string& id, EDataFlow flow,
                               IAudioClient** out,
                               IAudioClient3** out3) -> bool {
        IMMDevice* dev = nullptr;
        HRESULT r;
        if (id.empty()) {
            r = impl_->enumerator->GetDefaultAudioEndpoint(flow, eConsole, &dev);
        } else {
            std::wstring wid = utf8ToWide(id);
            r = impl_->enumerator->GetDevice(wid.c_str(), &dev);
        }
        if (FAILED(r)) return false;

        // Attempt IAudioClient3 upgrade in shared mode (exclusive uses IAudioClient).
        if (!impl_->exclusiveMode) {
            IAudioClient3* c3 = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient3), CLSCTX_ALL,
                                        nullptr,
                                        reinterpret_cast<void**>(&c3)))) {
                *out3 = c3;
                *out  = c3;   // implicit upcast: IAudioClient3* → IAudioClient*
                (*out)->AddRef(); // two references: one for *out, one for *out3
                dev->Release();
                return true;
            }
        }

        // Fall back to plain IAudioClient (older Windows or exclusive mode).
        r = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(out));
        dev->Release();
        return SUCCEEDED(r);
    };

    if (!activateClient(inId,  eCapture, &impl_->captureClient, &impl_->captureClient3)
     || !activateClient(outId, eRender,  &impl_->renderClient,  &impl_->renderClient3)) {
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
    // Helper: convert a blockSize (frames) to the nearest valid REFERENCE_TIME
    // period >= the device minimum period.  REFERENCE_TIME is in 100ns units.
    // For shared mode we pass the requested period as hnsBufferDuration so the
    // OS selects the smallest integer-multiple of the fundamental period that
    // fits blockSize frames.  For exclusive mode we must use the device minimum.
    auto computePeriod = [&](IAudioClient* client, UINT32 sampleRate,
                             bool exclusive) -> REFERENCE_TIME {
        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        if (FAILED(client->GetDevicePeriod(&defaultPeriod, &minPeriod)))
            return exclusive ? 0 : 0;  // fallback: let OS decide

        if (exclusive)
            return minPeriod;

        // Shared mode: convert blockSize to 100ns, round up to the next
        // integer multiple of the fundamental period (== minPeriod).
        if (sampleRate == 0 || impl_->blockSize <= 0)
            return defaultPeriod;

        // blockSize in 100ns units (avoid fp: multiply before divide).
        const REFERENCE_TIME requested =
            static_cast<REFERENCE_TIME>(impl_->blockSize) * 10000000LL
            / static_cast<REFERENCE_TIME>(sampleRate);

        // Clamp to [minPeriod, defaultPeriod].
        if (requested <= minPeriod)  return minPeriod;
        if (requested >= defaultPeriod) return defaultPeriod;

        // Round up to the nearest integer multiple of minPeriod.
        if (minPeriod > 0) {
            const REFERENCE_TIME multiples =
                (requested + minPeriod - 1) / minPeriod;
            const REFERENCE_TIME snapped = multiples * minPeriod;
            return snapped <= defaultPeriod ? snapped : defaultPeriod;
        }
        return requested;
    };

    const REFERENCE_TIME capturePeriod =
        computePeriod(impl_->captureClient,
                      impl_->captureFmt->nSamplesPerSec,
                      impl_->exclusiveMode);
    const REFERENCE_TIME renderPeriod =
        computePeriod(impl_->renderClient,
                      impl_->renderFmt->nSamplesPerSec,
                      impl_->exclusiveMode);

    // Lambda: initialise one client, preferring IAudioClient3 in shared mode.
    // Falls back to IAudioClient::Initialize if IAudioClient3 is unavailable or
    // if InitializeSharedAudioStream fails at runtime.
    auto initClient = [&](IAudioClient* client, IAudioClient3* client3,
                          WAVEFORMATEX* fmt, REFERENCE_TIME period,
                          bool exclusive) -> HRESULT {
        if (!exclusive && client3) {
            // IAudioClient3 path: ask the engine for its minimum period in frames.
            UINT32 defPer = 0, fundPer = 0, minPer = 0, maxPer = 0;
            HRESULT r = client3->GetSharedModeEnginePeriod(
                fmt, &defPer, &fundPer, &minPer, &maxPer);
            if (SUCCEEDED(r))
                r = client3->InitializeSharedAudioStream(
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    minPer, fmt, nullptr);
            if (SUCCEEDED(r)) return r;  // success — low-latency path
            // Fall through to IAudioClient on any failure.
        }
        return client->Initialize(
            exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            period, exclusive ? period : 0,
            fmt, nullptr);
    };

    hr = initClient(impl_->captureClient, impl_->captureClient3,
                    impl_->captureFmt, capturePeriod, impl_->exclusiveMode);
    if (FAILED(hr)) {
        setErr("IAudioClient::Initialize failed for capture");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    hr = initClient(impl_->renderClient, impl_->renderClient3,
                    impl_->renderFmt, renderPeriod, impl_->exclusiveMode);
    if (FAILED(hr)) {
        setErr("IAudioClient::Initialize failed for render");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // Query combined stream latency from both clients.
    // GetStreamLatency() is only valid after Initialize() succeeds.
    {
        REFERENCE_TIME capLat = 0, renLat = 0;
        impl_->captureClient->GetStreamLatency(&capLat);
        impl_->renderClient->GetStreamLatency(&renLat);
        // REFERENCE_TIME is in 100-nanosecond units; convert to milliseconds.
        impl_->streamLatencyMs.store(
            static_cast<double>(capLat + renLat) / 10000.0,
            std::memory_order_relaxed);
    }

    // Capture uses an event-driven callback; render is polled from the capture
    // callback (capture-driven duplex loop), so no render event is needed.
    impl_->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->captureEvent) {
        setErr("CreateEvent failed for capture");
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    impl_->captureClient->SetEventHandle(impl_->captureEvent);

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
    // Pre-roll render buffer with silence (one device period, not the full buffer)
    // Pre-rolling the full buffer would add one full buffer period (~10 ms) of
    // unnecessary startup latency; one period is sufficient to avoid the initial
    // underrun glitch.
    // -----------------------------------------------------------------------
    UINT32 renderBufSize = 0;
    impl_->renderClient->GetBufferSize(&renderBufSize);
    {
        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        impl_->renderClient->GetDevicePeriod(&defaultPeriod, &minPeriod);
        const REFERENCE_TIME period = impl_->exclusiveMode ? minPeriod : defaultPeriod;
        UINT32 prerollFrames = renderBufSize; // fallback: full buffer if period unknown
        if (period > 0) {
            prerollFrames = static_cast<UINT32>(
                period * impl_->renderFmt->nSamplesPerSec / 10000000LL);
            if (prerollFrames > renderBufSize)
                prerollFrames = renderBufSize;
        }
        BYTE* silenceData = nullptr;
        if (SUCCEEDED(impl_->renderService->GetBuffer(prerollFrames, &silenceData)))
            impl_->renderService->ReleaseBuffer(prerollFrames,
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
        // Size the output buffer to the worst-case resampled frame count:
        // - Ceiling of (captureBufSize * outRate / inRate) for the ratio term.
        // - Plus the resampler's own output-side filter latency (depends on
        //   quality level and rate ratio — not a fixed constant).
        // - Plus one extra capture period as a safety margin for timing jitter.
        const spx_uint32_t resamplerLatency =
            speex_resampler_get_output_latency(impl_->captureResampler);
        const UINT32 ratioCeil =
            static_cast<UINT32>(
                (static_cast<uint64_t>(captureBufSize)
                 * impl_->renderFmt->nSamplesPerSec
                 + impl_->captureFmt->nSamplesPerSec - 1)
                / impl_->captureFmt->nSamplesPerSec);
        const UINT32 maxOut = ratioCeil + resamplerLatency + captureBufSize;
        impl_->resampledBuf.assign(maxOut * 2, 0.f);
    }

    // Allocate the render drain ring buffer — sized for 4 render periods so
    // timing jitter never causes processed frames to be discarded.
    // lv2_ringbuffer requires a power-of-2 byte size.
    {
        const size_t drainBytes = static_cast<size_t>(4)
            * std::max(captureBufSize, renderBufSize)
            * renChannels * sizeof(float);
        size_t ringSize = 1;
        while (ringSize < drainBytes) ringSize <<= 1;
        impl_->renderDrainRing = lv2_ringbuffer_create(ringSize);
        if (!impl_->renderDrainRing) {
            setErr("lv2_ringbuffer_create failed for render drain buffer");
            impl_->state = AudioEngine::State::Error;
            releaseResources();
            return;
        }
    }

    // Initialise drift-correction state.
    // Target: keep render buffer at ~one device period ahead (low latency).
    // Correction granularity: one capture packet.
    impl_->driftTarget   = std::max(captureBufSize, UINT32(1));
    impl_->driftPeriodFr = captureBufSize;
    impl_->driftPackets  = 0;
    impl_->driftFillSum  = 0;

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

    // Fast-path flags: constant for the entire session.
    // inputFastPath:  capture is already stereo float and no SRC needed —
    //                 copy WASAPI buffer directly to stereoBuf, skipping inBuf.
    // outputFastPath: render is stereo float — push dspOutBuf directly to the
    //                 drain ring, skipping the outBuf upmix intermediate.
    const bool inputFastPath =
        (impl_->captureFmt->nChannels == 2)
        && isFloatFormat(impl_->captureFmt)
        && !impl_->captureResampler;
    const bool outputFastPath =
        (impl_->renderFmt->nChannels == 2)
        && isFloatFormat(impl_->renderFmt);

    // Cache buffer size — constant after Initialize(), no need to query per packet.
    UINT32 renderBufSize = 0;
    impl_->renderClient->GetBufferSize(&renderBufSize);

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

            // Hoist into const locals so the compiler sees loop-invariant values
            // and can eliminate the per-iteration branches in the downmix/upmix loops.
            const UINT32 capChannels = impl_->captureFmt->nChannels;
            const UINT32 renChannels = impl_->renderFmt->nChannels;

            bool silent = (captureFlags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

            if (captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                impl_->dropouts.fetch_add(1, std::memory_order_relaxed);

            // --- Input: convert capture data → stereoBuf ---
            if (inputFastPath) {
                // Fast path: capture is already stereo float.
                // Copy directly to stereoBuf, skipping inBuf entirely.
                if (silent)
                    std::fill(impl_->stereoBuf.begin(),
                              impl_->stereoBuf.begin() + numFrames * 2, 0.f);
                else
                    std::memcpy(impl_->stereoBuf.data(), captureData,
                                numFrames * 2 * sizeof(float));
            } else {
                // General path: convert to inBuf first, then downmix.
                if (silent)
                    std::fill(impl_->inBuf.begin(),
                              impl_->inBuf.begin() + numFrames * capChannels, 0.f);
                else
                    toFloat(captureData, numFrames, capChannels,
                            impl_->captureFmt, impl_->inBuf.data());
            }

            impl_->captureService->ReleaseBuffer(numFrames);

            if (!inputFastPath) {
                // Downmix capture to stereo (2-ch interleaved, capture rate).
                if (capChannels >= 2) {
                    for (UINT32 f = 0; f < numFrames; ++f) {
                        impl_->stereoBuf[f * 2 + 0] = impl_->inBuf[f * capChannels + 0];
                        impl_->stereoBuf[f * 2 + 1] = impl_->inBuf[f * capChannels + 1];
                    }
                } else if (capChannels == 1) {
                    for (UINT32 f = 0; f < numFrames; ++f) {
                        impl_->stereoBuf[f * 2 + 0] = impl_->inBuf[f];
                        impl_->stereoBuf[f * 2 + 1] = impl_->inBuf[f];
                    }
                } else {
                    std::fill(impl_->stereoBuf.begin(),
                              impl_->stereoBuf.begin() + numFrames * 2, 0.f);
                }
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

            // --- Output: upmix / push to drain ring ---
            if (outputFastPath) {
                // Fast path: render is stereo float.
                // Push dspOutBuf directly into the drain ring, skipping outBuf.
                const size_t pushBytes = static_cast<size_t>(dspFrames) * 2 * sizeof(float);
                const size_t written = lv2_ringbuffer_write(
                    impl_->renderDrainRing,
                    reinterpret_cast<const char*>(impl_->dspOutBuf.data()),
                    pushBytes);
                if (written < pushBytes)
                    impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
            } else {
                // General path: upmix stereo → renChannels, then push outBuf.
                for (UINT32 f = 0; f < dspFrames; ++f) {
                    for (UINT32 c = 0; c < renChannels; ++c) {
                        impl_->outBuf[f * renChannels + c] =
                            (c < 2) ? impl_->dspOutBuf[f * 2 + c] : 0.f;
                    }
                }
                const size_t pushBytes =
                    static_cast<size_t>(dspFrames) * renChannels * sizeof(float);
                const size_t written = lv2_ringbuffer_write(
                    impl_->renderDrainRing,
                    reinterpret_cast<const char*>(impl_->outBuf.data()),
                    pushBytes);
                if (written < pushBytes)
                    impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
            }

            // Drain as many frames as the render client will accept.
            {
                UINT32 padding = 0;
                impl_->renderClient->GetCurrentPadding(&padding);
                const UINT32 available = renderBufSize - padding;

                // --- Drift correction (fill-level feedback) ---
                // Accumulate fill samples and every kDriftWindow packets apply
                // a one-packet correction if the average deviates significantly
                // from the target fill level.
                impl_->driftFillSum += padding;
                ++impl_->driftPackets;
                if (impl_->driftPackets >= Impl::kDriftWindow) {
                    const uint32_t avgFill =
                        static_cast<uint32_t>(impl_->driftFillSum / impl_->driftPackets);
                    impl_->driftPackets = 0;
                    impl_->driftFillSum = 0;

                    const uint32_t thresh = impl_->driftPeriodFr;
                    const size_t frameBytes =
                        static_cast<size_t>(renChannels) * sizeof(float);

                    if (avgFill > impl_->driftTarget + thresh) {
                        // Render buffer filling faster than expected:
                        // capture clock slightly faster than render.
                        // Discard one packet's worth of frames from the ring.
                        const size_t discardBytes =
                            static_cast<size_t>(impl_->driftPeriodFr) * frameBytes;
                        const size_t canDiscard =
                            std::min(discardBytes,
                                     lv2_ringbuffer_read_space(impl_->renderDrainRing));
                        if (canDiscard > 0) {
                            // Advance the read pointer without copying.
                            lv2_ringbuffer_read(impl_->renderDrainRing, nullptr, canDiscard);
                            impl_->dropouts.fetch_add(1, std::memory_order_relaxed);
                        }
                    } else if (impl_->driftTarget > thresh &&
                               avgFill + thresh < impl_->driftTarget) {
                        // Render buffer draining faster than expected:
                        // render clock slightly faster than capture.
                        // Inject one packet of silence directly to the render client.
                        UINT32 silPadding = 0;
                        impl_->renderClient->GetCurrentPadding(&silPadding);
                        const UINT32 silAvail = renderBufSize - silPadding;
                        const UINT32 silFrames =
                            std::min(impl_->driftPeriodFr, silAvail);
                        if (silFrames > 0) {
                            BYTE* silData = nullptr;
                            if (SUCCEEDED(impl_->renderService->GetBuffer(
                                    silFrames, &silData)))
                                impl_->renderService->ReleaseBuffer(
                                    silFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                        }
                    }
                }

                const size_t frameBytes =
                    static_cast<size_t>(renChannels) * sizeof(float);
                const size_t ringBytes =
                    lv2_ringbuffer_read_space(impl_->renderDrainRing);
                const UINT32 ringFrames =
                    static_cast<UINT32>(ringBytes / frameBytes);
                const UINT32 toWrite = std::min(ringFrames, available);

                if (toWrite > 0) {
                    // Read ring → outBuf (reuse existing scratch), then convert.
                    lv2_ringbuffer_read(
                        impl_->renderDrainRing,
                        reinterpret_cast<char*>(impl_->outBuf.data()),
                        static_cast<size_t>(toWrite) * frameBytes);

                    BYTE* renderData = nullptr;
                    hr = impl_->renderService->GetBuffer(toWrite, &renderData);
                    if (FAILED(hr)) {
                        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) return false;
                    } else {
                        fromFloat(impl_->outBuf.data(), toWrite, renChannels,
                                  impl_->renderFmt, renderData);
                        impl_->renderService->ReleaseBuffer(toWrite, 0);
                    }
                }
            }
        }
    }
}

void AudioEngine::releaseResources() {
    if (impl_->renderDrainRing) {
        lv2_ringbuffer_free(impl_->renderDrainRing);
        impl_->renderDrainRing = nullptr;
    }
    if (impl_->captureResampler) {
        speex_resampler_destroy(impl_->captureResampler);
        impl_->captureResampler = nullptr;
    }
    if (impl_->captureService) { impl_->captureService->Release(); impl_->captureService = nullptr; }
    if (impl_->renderService)  { impl_->renderService->Release();  impl_->renderService  = nullptr; }
    // Release IAudioClient3 *before* captureClient/renderClient.
    // Both pointers may refer to the same COM object (AddRef'd twice in activateClient);
    // releasing the IAudioClient3 ref first brings the count to 1, then the
    // IAudioClient release brings it to 0 and destroys the object.
    if (impl_->captureClient3) { impl_->captureClient3->Release(); impl_->captureClient3 = nullptr; }
    if (impl_->renderClient3)  { impl_->renderClient3->Release();  impl_->renderClient3  = nullptr; }
    if (impl_->captureClient)  { impl_->captureClient->Release();  impl_->captureClient  = nullptr; }
    if (impl_->renderClient)   { impl_->renderClient->Release();   impl_->renderClient   = nullptr; }
    if (impl_->enumerator)     { impl_->enumerator->Release();     impl_->enumerator     = nullptr; }
    if (impl_->captureFmt)     { CoTaskMemFree(impl_->captureFmt); impl_->captureFmt     = nullptr; }
    if (impl_->renderFmt)      { CoTaskMemFree(impl_->renderFmt);  impl_->renderFmt      = nullptr; }
    if (impl_->captureEvent)   { CloseHandle(impl_->captureEvent); impl_->captureEvent   = nullptr; }
    if (impl_->stopEvent)      { CloseHandle(impl_->stopEvent);    impl_->stopEvent      = nullptr; }
}

