#include "PluginSlot.h"

bool PluginSlot::create(HWND parent, int id, const RECT& bounds) {
    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "STATIC",
        "Empty Slot",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandle(nullptr),
        nullptr);

    return hwnd_ != nullptr;
}

void PluginSlot::setLabel(const char* text) {
    if (hwnd_)
        SetWindowTextA(hwnd_, text);
}

void PluginSlot::resize(const RECT& bounds) {
    if (hwnd_)
        MoveWindow(hwnd_,
                   bounds.left, bounds.top,
                   bounds.right - bounds.left,
                   bounds.bottom - bounds.top,
                   TRUE);
}

HWND PluginSlot::hwnd() const {
    return hwnd_;
}
