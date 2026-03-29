#pragma once

#include <windows.h>

class ControlBar {
public:
    bool create(HWND parent, const RECT& bounds);
    HWND hwnd() const;

private:
    HWND hwnd_ = nullptr;
};
