#include "PluginSlot.h"

bool PluginSlot::create(HWND parent, int id, const RECT& bounds) {
    hwnd_ = CreateWindowExA(
        0,
        "STATIC",
        "Plugin Slot",
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

HWND PluginSlot::hwnd() const {
    return hwnd_;
}
