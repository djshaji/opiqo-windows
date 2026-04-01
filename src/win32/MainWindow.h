#pragma once

#include <memory>
#include <windows.h>

#include "AppSettings.h"
#include "WasapiDeviceEnum.h"

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

    // Called (on the UI thread) whenever the device topology changes.
    void onDeviceListChanged();

    HINSTANCE instance_ = nullptr;
    HWND      hwnd_     = nullptr;

    AppSettings                    settings_;
    std::unique_ptr<WasapiDeviceEnum> deviceEnum_;
};
