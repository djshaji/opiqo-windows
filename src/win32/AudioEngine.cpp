#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "AudioEngine.h"
#include "../LiveEffectEngine.h"

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
    HANDLE stopEvent    = nullptr;

    // Pre-allocated scratch buffers (sized at start(), reused in loop).
    std::vector<float> inBuf;
    std::vector<float> outBuf;
    // Fixed stereo (2-ch) intermediate buffer passed to LiveEffectEngine::process().
    std::vector<float> stereoBuf;

    // DSP engine (non-owning pointer; nullptr = pass-through).
    LiveEffectEngine* engine = nullptr;

    // Thread and state.
    std::thread              thread;
    std::atomic<AudioEngine::State> state { AudioEngine::State::Off };
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
    } else {
        // Fallback: 16-bit PCM -> float
        const int16_t* s = reinterpret_cast<const int16_t*>(src);
        UINT32 n = frames * channels;
        for (UINT32 i = 0; i < n; ++i)
            dst[i] = static_cast<float>(s[i]) / 32768.f;
    }
}

// Write interleaved float to a WASAPI render buffer.
static void fromFloat(const float* src, UINT32 frames, UINT32 channels,
                      const WAVEFORMATEX* fmt, BYTE* dst) {
    if (isFloatFormat(fmt)) {
        std::memcpy(dst, src, frames * channels * sizeof(float));
    } else {
        int16_t* d = reinterpret_cast<int16_t*>(dst);
        UINT32 n = frames * channels;
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

std::string AudioEngine::errorMessage() const { return impl_->errorMsg; }

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
    // Carry device IDs into the audio thread via errorMsg before state moves.
    impl_->errorMsg = inputDeviceId + "|" + outputDeviceId;

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
    // Extract device IDs from the temporary carrier in errorMsg.
    std::string carrier = impl_->errorMsg;
    impl_->errorMsg.clear();

    std::string inId, outId;
    auto sep = carrier.find('|');
    if (sep != std::string::npos) {
        inId  = carrier.substr(0, sep);
        outId = carrier.substr(sep + 1);
    }

    // COM init for the audio thread — multithreaded apartment.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        impl_->errorMsg = "Audio thread CoInitializeEx failed";
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
        impl_->errorMsg = "CoCreateInstance(IMMDeviceEnumerator) failed";
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
        impl_->errorMsg = "Failed to activate audio endpoints";
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
        impl_->errorMsg = "GetMixFormat failed";
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // -----------------------------------------------------------------------
    // Initialise both clients in event-driven shared mode
    // -----------------------------------------------------------------------
    // Use the device's own preferred period (0 = let driver decide).
    AUDCLNT_SHAREMODE shareMode = impl_->exclusiveMode
        ? AUDCLNT_SHAREMODE_EXCLUSIVE
        : AUDCLNT_SHAREMODE_SHARED;

    REFERENCE_TIME period = 0;  // 0 → default device period in shared mode.

    hr = impl_->captureClient->Initialize(
        shareMode,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        period, 0, impl_->captureFmt, nullptr);
    if (FAILED(hr)) {
        impl_->errorMsg = "IAudioClient::Initialize failed for capture";
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    hr = impl_->renderClient->Initialize(
        shareMode,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        period, 0, impl_->renderFmt, nullptr);
    if (FAILED(hr)) {
        impl_->errorMsg = "IAudioClient::Initialize failed for render";
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    // Capture event (auto-reset — signalled by WASAPI each period).
    impl_->captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!impl_->captureEvent) {
        impl_->errorMsg = "CreateEvent failed for capture";
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    impl_->captureClient->SetEventHandle(impl_->captureEvent);
    impl_->renderClient->SetEventHandle(impl_->captureEvent);  // Shared event.

    // -----------------------------------------------------------------------
    // Get capture and render services
    // -----------------------------------------------------------------------
    hr = impl_->captureClient->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(&impl_->captureService));
    if (FAILED(hr)) {
        impl_->errorMsg = "GetService(IAudioCaptureClient) failed";
        impl_->state = AudioEngine::State::Error;
        releaseResources();
        return;
    }

    hr = impl_->renderClient->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(&impl_->renderService));
    if (FAILED(hr)) {
        impl_->errorMsg = "GetService(IAudioRenderClient) failed";
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
    // Allocate scratch buffers (max frames = render buffer size, stereo)
    // -----------------------------------------------------------------------
    UINT32 maxChannels = std::max(impl_->captureFmt->nChannels,
                                  impl_->renderFmt->nChannels);
    impl_->inBuf.assign(renderBufSize * maxChannels, 0.f);
    impl_->outBuf.assign(renderBufSize * maxChannels, 0.f);
    // Stereo buffer for LiveEffectEngine (always 2 channels).
    impl_->stereoBuf.assign(renderBufSize * 2, 0.f);

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

            if (impl_->engine) {
                // Downmix capture to stereo for LiveEffectEngine.
                for (UINT32 f = 0; f < numFrames; ++f) {
                    impl_->stereoBuf[f * 2 + 0] =
                        (capChannels >= 1) ? impl_->inBuf[f * capChannels + 0] : 0.f;
                    impl_->stereoBuf[f * 2 + 1] =
                        (capChannels >= 2) ? impl_->inBuf[f * capChannels + 1] : impl_->stereoBuf[f * 2];
                }

                // DSP processing — stereo in, stereo out.
                impl_->engine->process(
                    impl_->stereoBuf.data(),
                    impl_->stereoBuf.data() + numFrames * 2,  // non-overlapping output half
                    static_cast<int>(numFrames));

                // Upmix stereo output back to render channel count.
                const float* stereoOut = impl_->stereoBuf.data() + numFrames * 2;
                for (UINT32 f = 0; f < numFrames; ++f) {
                    for (UINT32 c = 0; c < renChannels; ++c) {
                        impl_->outBuf[f * renChannels + c] =
                            (c < 2) ? stereoOut[f * 2 + c] : 0.f;
                    }
                }
            } else {
                // No engine wired — plain pass-through.
                UINT32 copyChannels = std::min(capChannels, renChannels);
                for (UINT32 f = 0; f < numFrames; ++f) {
                    for (UINT32 c = 0; c < renChannels; ++c) {
                        impl_->outBuf[f * renChannels + c] =
                            (c < copyChannels)
                                ? impl_->inBuf[f * capChannels + c]
                                : 0.f;
                    }
                }
            }

            // Write to render buffer.
            UINT32 padding = 0;
            impl_->renderClient->GetCurrentPadding(&padding);
            UINT32 renderBufSize = 0;
            impl_->renderClient->GetBufferSize(&renderBufSize);
            UINT32 available = renderBufSize - padding;
            UINT32 toWrite   = std::min(numFrames, available);

            BYTE* renderData = nullptr;
            hr = impl_->renderService->GetBuffer(toWrite, &renderData);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) return false;
                continue;
            }

            fromFloat(impl_->outBuf.data(), toWrite, renChannels,
                      impl_->renderFmt, renderData);
            impl_->renderService->ReleaseBuffer(toWrite, 0);
        }
    }
}

void AudioEngine::releaseResources() {
    if (impl_->captureService) { impl_->captureService->Release(); impl_->captureService = nullptr; }
    if (impl_->renderService)  { impl_->renderService->Release();  impl_->renderService  = nullptr; }
    if (impl_->captureClient)  { impl_->captureClient->Release();  impl_->captureClient  = nullptr; }
    if (impl_->renderClient)   { impl_->renderClient->Release();   impl_->renderClient   = nullptr; }
    if (impl_->enumerator)     { impl_->enumerator->Release();     impl_->enumerator     = nullptr; }
    if (impl_->captureFmt)     { CoTaskMemFree(impl_->captureFmt); impl_->captureFmt     = nullptr; }
    if (impl_->renderFmt)      { CoTaskMemFree(impl_->renderFmt);  impl_->renderFmt      = nullptr; }
    if (impl_->captureEvent)   { CloseHandle(impl_->captureEvent); impl_->captureEvent   = nullptr; }
    if (impl_->stopEvent)      { CloseHandle(impl_->stopEvent);    impl_->stopEvent      = nullptr; }
}

