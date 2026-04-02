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

    // Power toggle — latching pushbutton.
    powerButton_ = CreateWindowExA(
        0, "BUTTON", "Power",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE,
        4, 4, 80, 28,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_POWER_TOGGLE)),
        hInst, nullptr);

    // Gain slider (0–100, default 80).
    gainSlider_ = CreateWindowExA(
        0, TRACKBAR_CLASSA, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        92, 8, 120, 20,
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
        220, 4, 80, 28,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_RECORD_TOGGLE)),
        hInst, nullptr);

    // Format dropdown (WAV / MP3 / OGG).
    formatCombo_ = CreateWindowExA(
        0, "COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        308, 4, 100, 120,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_FORMAT_COMBO)),
        hInst, nullptr);
    if (formatCombo_) {
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("WAV"));
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("MP3"));
        SendMessageA(formatCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("OGG"));
        SendMessageA(formatCombo_, CB_SETCURSEL, 0, 0);
    }

    // Quality dropdown (High / Medium / Low) — hidden by default (WAV selected).
    qualityCombo_ = CreateWindowExA(
        0, "COMBOBOX", nullptr,
        WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        416, 4, 90, 120,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_QUALITY_COMBO)),
        hInst, nullptr);
    if (qualityCombo_) {
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("High"));
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Medium"));
        SendMessageA(qualityCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Low"));
        SendMessageA(qualityCombo_, CB_SETCURSEL, 0, 0);
    }

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
    if (hwnd_)
        MoveWindow(hwnd_,
                   bounds.left, bounds.top,
                   bounds.right - bounds.left,
                   bounds.bottom - bounds.top,
                   TRUE);
}

HWND ControlBar::hwnd() const {
    return hwnd_;
}
