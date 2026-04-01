#pragma once

#include <string>

struct AppSettings {
    std::string inputDeviceId;
    std::string outputDeviceId;
    int  sampleRate    = 48000;
    int  blockSize     = 4096;
    bool exclusiveMode = false;
    int  recordFormat  = 0;  // 0 = WAV, 1 = MP3, 2 = OGG
    int  recordQuality = 0;  // 0 = default / CBR 128

    // Loads from %APPDATA%\Opiqo\settings.json; returns defaults on any error.
    static AppSettings load();

    // Saves to %APPDATA%\Opiqo\settings.json; silently ignores write errors.
    void save() const;
};
