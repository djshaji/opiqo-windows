#include "LiveEffectEngine.h"
#include <windows.h>
#include <filesystem>
#include <libloaderapi.h>

int main() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::current_path(exeDir);
    
    LiveEffectEngine engine;
    engine.initLV2();
    std::string cwd = std::filesystem::current_path().string();
    engine.initPlugins(cwd + "\\lv2");
    std::string pluginInfo = engine.getAvailablePlugins();
    
    return 0;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main();
}