#include "AudioEngine.h"

bool AudioEngine::start(int32_t,
                        int32_t,
                        const std::string&,
                        const std::string&,
                        bool) {
    state_ = State::Running;
    return true;
}

void AudioEngine::stop() {
    state_ = State::Off;
}

AudioEngine::State AudioEngine::state() const {
    return state_;
}
