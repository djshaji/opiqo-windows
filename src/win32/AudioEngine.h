#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class AudioEngine {
public:
    enum class State {
        Off,
        Starting,
        Running,
        Stopping,
        Error,
    };

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Start the duplex WASAPI stream with the given parameters.
    // Returns true if the audio thread was launched successfully.
    // On failure the engine stays in (or moves to) State::Error;
    // call errorMessage() for a human-readable reason.
    bool start(int32_t sampleRate,
               int32_t blockSize,
               const std::string& inputDeviceId,
               const std::string& outputDeviceId,
               bool exclusiveMode);

    // Signal the audio thread to stop and block until it exits.
    void stop();

    State       state()        const;
    int32_t     sampleRate()   const;
    int32_t     blockSize()    const;
    std::string errorMessage() const;

private:
    // Audio-thread entry point.
    void audioThreadProc();

    // WASAPI pass-through loop called from audioThreadProc().
    // Returns false if the stream must stop due to device loss.
    bool runLoop();

    // Release all WASAPI COM objects and the stop event.
    void releaseResources();

    struct Impl;
    Impl* impl_ = nullptr;
};
