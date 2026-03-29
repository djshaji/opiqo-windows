#pragma once

#include <cstdint>
#include <string>

class AudioEngine {
public:
    enum class State {
        Off,
        Starting,
        Running,
        Stopping,
        Error,
    };

    bool start(int32_t sampleRate,
               int32_t blockSize,
               const std::string& inputDeviceId,
               const std::string& outputDeviceId,
               bool exclusiveMode);
    void stop();
    State state() const;

private:
    State state_ = State::Off;
};
