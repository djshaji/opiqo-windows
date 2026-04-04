#include "ControlBar.h"
#include "resource.h"
#include <commctrl.h>

bool ControlBar::create(HWND parent, const RECT& bounds) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    int w = bounds.right - bounds.left;
    int h = bounds.bottom - bounds.top;

    hwnd_ = CreateWindowExA(
        0, "STATIC", nullptr,
        WS_CHILD | WS_VISIBLE,
        bounds.left, bounds.top, w, h,
        parent, nullptr, hInst, nullptr);

    if (!hwnd_)
        return false;

    // Helper: scale a logical-96-dpi pixel value to the window's actual DPI.
#define S(px) MulDiv((px), GetDpiForWindow(hwnd_), 96)

    // Power toggle — latching pushbutton.
    powerButton_ = CreateWindowExA(
        0, "BUTTON", "Power",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        S(4), S(4), S(80), S(28),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_POWER_TOGGLE)),
        hInst, nullptr);

    // Gain slider (0–100, default 80).
    gainSlider_ = CreateWindowExA(
        0, TRACKBAR_CLASSA, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        S(92), S(8), S(120), S(20),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_GAIN_SLIDER)),
        hInst, nullptr);
    if (gainSlider_) {
        SendMessage(gainSlider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessage(gainSlider_, TBM_SETPOS,   TRUE, 80);
    }

    // Record toggle — latching pushbutton.
    recordButton_ = CreateWindowExA(
        0, "BUTTON", "Record",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        S(220), S(4), S(80), S(28),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RECORD_TOGGLE)),
        hInst, nullptr);

    // Format dropdown (WAV / MP3 / OGG).
    formatCombo_ = CreateWindowExA(
        0, "COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(308), S(4), S(100), S(120),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FORMAT_COMBO)),
        hInst, nullptr);
    if (formatCombo_) {
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("WAV"));
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("MP3"));
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("OGG"));
        SendMessageA(formatCombo_, CB_SETCURSEL, 0, 0);
    }

    // Quality dropdown (High / Medium / Low).
    qualityCombo_ = CreateWindowExA(
        0, "COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        S(416), S(4), S(90), S(120),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_QUALITY_COMBO)),
        hInst, nullptr);
    if (qualityCombo_) {
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("High"));
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Medium"));
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Low"));
        SendMessageA(qualityCombo_, CB_SETCURSEL, 0, 0);
    }

#undef S

    return powerButton_ != nullptr;
}

void ControlBar::setPowerState(bool on) {
    if (powerButton_)
        SendMessage(powerButton_, BM_SETCHECK,
                    on ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ControlBar::setRecordState(bool on) {
    if (recordButton_) {
        SendMessage(recordButton_, BM_SETCHECK,
                    on ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextA(recordButton_, on ? "\u25a0 Stop" : "\u25cf Record");
    }
}

int ControlBar::gainValue() const {
    if (!gainSlider_) return 80;
    return static_cast<int>(SendMessage(gainSlider_, TBM_GETPOS, 0, 0));
}

void ControlBar::setGainValue(int pos) {
    if (!gainSlider_) return;
    if (pos < 0)   pos = 0;
    if (pos > 100) pos = 100;
    SendMessage(gainSlider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(pos));
}

int ControlBar::formatIndex() const {
    if (!formatCombo_) return 0;
    int sel = static_cast<int>(SendMessage(formatCombo_, CB_GETCURSEL, 0, 0));
    return (sel == CB_ERR) ? 0 : sel;
}

void ControlBar::setFormatIndex(int index) {
    if (formatCombo_)
        SendMessage(formatCombo_, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

int ControlBar::qualityIndex() const {
    if (!qualityCombo_) return 0;
    int sel = static_cast<int>(SendMessage(qualityCombo_, CB_GETCURSEL, 0, 0));
    return (sel == CB_ERR) ? 0 : sel;
}

void ControlBar::showQualityCombo(bool show) {
    if (qualityCombo_)
        ShowWindow(qualityCombo_, show ? SW_SHOW : SW_HIDE);
}

void ControlBar::enableRecordButton(bool enable) {
    if (recordButton_)
        EnableWindow(recordButton_, enable ? TRUE : FALSE);
}

void ControlBar::resize(const RECT& bounds) {
    if (!hwnd_) return;
    const int w = bounds.right - bounds.left;
    const int h = bounds.bottom - bounds.top;
    MoveWindow(hwnd_, bounds.left, bounds.top, w, h, TRUE);

    // Reposition children scaled to the current monitor DPI.
#define S(px) MulDiv((px), GetDpiForWindow(hwnd_), 96)
    if (powerButton_)  MoveWindow(powerButton_,  S(4),   S(4), S(80),  S(28), TRUE);
    if (gainSlider_)   MoveWindow(gainSlider_,   S(92),  S(8), S(120), S(20), TRUE);
    if (recordButton_) MoveWindow(recordButton_, S(220), S(4), S(80),  S(28), TRUE);
    if (formatCombo_)  MoveWindow(formatCombo_,  S(308), S(4), S(100), S(120),TRUE);
    if (qualityCombo_) MoveWindow(qualityCombo_, S(416), S(4), S(90),  S(120),TRUE);
#undef S
}

HWND ControlBar::hwnd() const {
    return hwnd_;
}
