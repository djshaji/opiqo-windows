#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "AppSettings.h"

#include "json.hpp"

#include <fstream>

static std::string settingsPath() {
    char appData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return std::string(appData) + "\\Opiqo\\settings.json";
}

AppSettings AppSettings::load() {
    AppSettings s;
    const std::string path = settingsPath();
    if (path.empty()) return s;

    std::ifstream f(path);
    if (!f.is_open()) return s;

    try {
        nlohmann::json j;
        f >> j;
        s.inputDeviceId  = j.value("inputDeviceId",  std::string{});
        s.outputDeviceId = j.value("outputDeviceId", std::string{});
        s.sampleRate     = j.value("sampleRate",     0);
        s.blockSize      = j.value("blockSize",      0);
        s.exclusiveMode  = j.value("exclusiveMode",  false);
        s.recordFormat   = j.value("recordFormat",   0);
        s.recordQuality  = j.value("recordQuality",  0);
        s.gain           = j.value("gain",            0.8f);
    } catch (...) {
        // Corrupt or unreadable — return defaults.
    }
    return s;
}

void AppSettings::save() const {
    const std::string path = settingsPath();
    if (path.empty()) return;

    // Ensure the directory exists (ignored if already present).
    const std::string dir = path.substr(0, path.rfind('\\'));
    CreateDirectoryA(dir.c_str(), nullptr);

    nlohmann::json j;
    j["inputDeviceId"]  = inputDeviceId;
    j["outputDeviceId"] = outputDeviceId;
    j["sampleRate"]     = sampleRate;
    j["blockSize"]      = blockSize;
    j["exclusiveMode"]  = exclusiveMode;
    j["recordFormat"]   = recordFormat;
    j["recordQuality"]  = recordQuality;
    j["gain"]           = gain;

    std::ofstream f(path);
    if (f.is_open()) f << j.dump(4);
}
