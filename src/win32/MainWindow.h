#pragma once

#include <memory>
#include <string>
#include <vector>
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

    // Called when the watchdog detects the engine entered State::Error mid-session.
    void onEngineError();

    // Called (on the UI thread) whenever the device topology changes.
    void onDeviceListChanged();

    // Recomputes and applies child-window positions for the current client size.
    void doLayout();

    // Creates (or recreates) the UI font at the window's current monitor DPI.
    void rebuildUiFont();

    // Sends WM_SETFONT to every descendant child window.
    void applyUiFont();

    // Called when the gain slider position changes.
    void onGainChanged();

    // --- Stress-test helpers ---
    void startStressTest();
    void stopStressTest();
    void stressTestTick();

    // Subclass proc for the ControlBar container — forwards WM_HSCROLL.
    static LRESULT CALLBACK ControlBarSubclassProc(HWND hwnd, UINT msg,
                                                   WPARAM wParam, LPARAM lParam,
                                                   UINT_PTR subclassId,
                                                   DWORD_PTR refData);

    HINSTANCE instance_ = nullptr;
    HWND      hwnd_     = nullptr;
    HWND      statusBar_ = nullptr;
    HFONT     uiFont_    = nullptr;  // 10pt Segoe UI, DPI-scaled

    AppSettings                       settings_;
    std::unique_ptr<WasapiDeviceEnum>  deviceEnum_;
    LiveEffectEngine                   liveEngine_;
    AudioEngine                        audioEngine_;
    ControlBar                         controlBar_;
    PluginSlot                         slots_[4];
    bool                               slotEnabled_[4] = { true, true, true, true };
    int                                recordingFd_    = -1;

    // --- Stress-test state ---
    std::vector<std::string> stressUris_;
    int                      stressIndex_  = 0;
    bool                     stressAdded_  = false;
    bool                     stressActive_ = false;
};
