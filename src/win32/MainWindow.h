#pragma once

#include <windows.h>

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    bool create(int nCmdShow);
    int run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
};
