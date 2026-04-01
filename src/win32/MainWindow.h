#pragma once

#include <memory>
#include <windows.h>

#include "AppSettings.h"
#include "AudioEngine.h"
#include "ControlBar.h"
#include "PluginDialog.h"
#include "PluginSlot.h"
#include "WasapiDeviceEnum.h"
#include "../LiveEffectEngine.h"

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    bool create(int nCmdShow);
    int  run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                                    WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // Called (on the UI thread) whenever the engine state poll fires.
    void onEngineStatePoll();

    // Called (on the UI thread) whenever the device topology changes.
    void onDeviceListChanged();

    // Recomputes and applies child-window positions for the current client size.
    void doLayout();

    HINSTANCE instance_ = nullptr;
    HWND      hwnd_     = nullptr;
    HWND      statusBar_ = nullptr;

    AppSettings                       settings_;
    std::unique_ptr<WasapiDeviceEnum>  deviceEnum_;
    LiveEffectEngine                   liveEngine_;
    AudioEngine                        audioEngine_;
    ControlBar                         controlBar_;
    PluginSlot                         slots_[4];
    bool                               slotEnabled_[4] = { true, true, true, true };
};
