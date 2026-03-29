#include "ControlBar.h"

bool ControlBar::create(HWND parent, const RECT& bounds) {
    hwnd_ = CreateWindowExA(
        0,
        "STATIC",
        "Control Bar (Milestone 0)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        bounds.left,
        bounds.top,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        parent,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr);

    return hwnd_ != nullptr;
}

HWND ControlBar::hwnd() const {
    return hwnd_;
}
