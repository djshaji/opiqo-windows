#pragma once

#include <string>

struct AppSettings {
    std::string inputDeviceId;
    std::string outputDeviceId;
    int  sampleRate    = 0;    // 0 = auto-detect from device on first run
    int  blockSize     = 0;    // 0 = auto → 512
    bool exclusiveMode = false;
    int   recordFormat  = 0;   // 0 = WAV, 1 = MP3, 2 = OGG
    int   recordQuality = 0;   // 0 = default / CBR 128
    float gain          = 0.8f; // master output gain [0.0, 1.0]

    // Pro license.
    std::string licenseKey;
    bool        activated = false;

    // Loads from %APPDATA%\Opiqo\settings.json; returns defaults on any error.
    static AppSettings load();

    // Saves to %APPDATA%\Opiqo\settings.json; silently ignores write errors.
    void save() const;
};
