#pragma once

#include <windows.h>

class PluginSlot {
public:
    bool create(HWND parent, int id, const RECT& bounds);
    HWND hwnd() const;

private:
    HWND hwnd_ = nullptr;
};
