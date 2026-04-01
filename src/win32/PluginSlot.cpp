#include "PluginSlot.h"
#include "resource.h"

static const char* kSlotClassName = "OpiqoPluginSlot";
static const char* kSlotNumbers[] = { "Slot 1", "Slot 2", "Slot 3", "Slot 4" };

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

bool PluginSlot::registerClass(HINSTANCE hInst) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = PluginSlot::SlotWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kSlotClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// Forward WM_COMMAND from child buttons up to MainWindow.
LRESULT CALLBACK PluginSlot::SlotWndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND) {
        HWND parent = GetParent(hwnd);
        if (parent) SendMessage(parent, msg, wParam, lParam);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Instance creation
// ---------------------------------------------------------------------------

bool PluginSlot::create(HWND parent, int slotIndex, const RECT& bounds) {
    slotIndex_ = slotIndex;
    HINSTANCE hInst = GetModuleHandle(nullptr);
    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;

    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        kSlotClassName, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        bounds.left, bounds.top, w, h,
        parent, nullptr, hInst, nullptr);

    if (!hwnd_) return false;

    const char* label = (slotIndex >= 0 && slotIndex < 4)
                        ? kSlotNumbers[slotIndex] : "Slot";

    labelStatic_ = CreateWindowExA(
        0, "STATIC", label,
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        4, 4, w - 8, 20,
        hwnd_, nullptr, hInst, nullptr);

    addButton_ = CreateWindowExA(
        0, "BUTTON", "Add Plugin",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        4, 30, 90, 24,
        hwnd_,
        reinterpret_cast<HMENU>(
            static_cast<UINT_PTR>(IDC_SLOT_ADD_BASE + slotIndex)),
        hInst, nullptr);

    bypassButton_ = CreateWindowExA(
        0, "BUTTON", "Bypass",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        100, 30, 70, 24,
        hwnd_,
        reinterpret_cast<HMENU>(
            static_cast<UINT_PTR>(IDC_SLOT_BYPASS_BASE + slotIndex)),
        hInst, nullptr);
    EnableWindow(bypassButton_, FALSE);

    deleteButton_ = CreateWindowExA(
        0, "BUTTON", "Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        176, 30, 70, 24,
        hwnd_,
        reinterpret_cast<HMENU>(
            static_cast<UINT_PTR>(IDC_SLOT_DELETE_BASE + slotIndex)),
        hInst, nullptr);
    EnableWindow(deleteButton_, FALSE);

    return addButton_ != nullptr;
}

// ---------------------------------------------------------------------------
// State control
// ---------------------------------------------------------------------------

void PluginSlot::setPlugin(const char* name) {
    if (labelStatic_) SetWindowTextA(labelStatic_, name);
    EnableWindow(bypassButton_, TRUE);
    EnableWindow(deleteButton_, TRUE);
}

void PluginSlot::clearPlugin() {
    if (labelStatic_)
        SetWindowTextA(labelStatic_,
            (slotIndex_ >= 0 && slotIndex_ < 4)
                ? kSlotNumbers[slotIndex_] : "Empty Slot");
    EnableWindow(bypassButton_, FALSE);
    EnableWindow(deleteButton_, FALSE);
    if (bypassButton_) SetWindowTextA(bypassButton_, "Bypass");
}

void PluginSlot::setBypassVisual(bool bypassed) {
    if (bypassButton_)
        SetWindowTextA(bypassButton_, bypassed ? "Enable" : "Bypass");
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void PluginSlot::resize(const RECT& bounds) {
    if (!hwnd_) return;
    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;
    MoveWindow(hwnd_, bounds.left, bounds.top, w, h, TRUE);
    if (labelStatic_)  MoveWindow(labelStatic_,  4,   4,  w - 8, 20, TRUE);
    if (addButton_)    MoveWindow(addButton_,    4,   30, 90,    24, TRUE);
    if (bypassButton_) MoveWindow(bypassButton_, 100, 30, 70,    24, TRUE);
    if (deleteButton_) MoveWindow(deleteButton_, 176, 30, 70,    24, TRUE);
}

HWND PluginSlot::hwnd() const { return hwnd_; }
