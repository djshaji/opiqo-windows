#pragma once

#include <windows.h>

class PluginSlot {
public:
    bool create(HWND parent, int id, const RECT& bounds);
    HWND hwnd() const;

    // Update the slot header label (e.g. slot number or active plugin name).
    void setLabel(const char* text);

    // Reposition and resize the slot panel.
    void resize(const RECT& bounds);

private:
    HWND hwnd_ = nullptr;
};
