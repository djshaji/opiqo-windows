#include "ParameterPanel.h"
#include "resource.h"
#include "../LiveEffectEngine.h"

#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

static const char* kPanelClass = "OpiqoParamPanel";

// Layout constants
static const int kRowH   = 28;   // height of each control row (px)
static const int kLabelW = 130;  // width of label column (px)
static const int kCtrlW  = 200;  // default width of the control widget (px)
static const int kPadX   = 4;    // horizontal padding (px)
static const int kPadY   = 4;    // vertical gap between rows (px)

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

bool ParameterPanel::registerClass(HINSTANCE hInst) {
    WNDCLASSA wc      = {};
    wc.lpfnWndProc    = ParameterPanel::PanelWndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = kPanelClass;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    return RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK ParameterPanel::PanelWndProc(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
    ParameterPanel* self = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<ParameterPanel*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ParameterPanel*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self) return self->handleMessage(msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT ParameterPanel::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HSCROLL:
            onHScroll(reinterpret_cast<HWND>(lParam), LOWORD(wParam));
            return 0;
        case WM_COMMAND:
            onCommand(LOWORD(wParam), HIWORD(wParam),
                      reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_VSCROLL:
            onVScroll(LOWORD(wParam), HIWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL:
            onMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_SIZE:
            updateScrollInfo();
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd_, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

bool ParameterPanel::build(HWND parent, LiveEffectEngine* engine, int slot,
                           const std::vector<LV2Plugin::PortInfo>& ports,
                           int baseId) {
    engine_ = engine;
    slot_   = slot;
    baseId_ = baseId;

    HINSTANCE hInst = GetModuleHandle(nullptr);

    if (!hwnd_) {
        hwnd_ = CreateWindowExA(
            0, kPanelClass, nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0,
            parent, nullptr, hInst, this);
        if (!hwnd_) return false;
    }

    // Destroy any previously built controls.
    clear();

    ShowWindow(hwnd_, SW_SHOW);

    int  y      = kPadY;
    UINT nextId = static_cast<UINT>(baseId_);

    for (const auto& pi : ports) {
        ControlEntry e;
        e.portIndex   = static_cast<int>(pi.portIndex);
        e.type        = pi.type;
        e.minVal      = pi.minVal;
        e.maxVal      = pi.maxVal;
        e.symbol      = pi.symbol;
        e.writableUri = pi.writableUri;
        e.controlId   = nextId++;

        // Label (always created; no command ID needed)
        e.labelHwnd = CreateWindowExA(
            0, "STATIC", pi.label.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
            kPadX, y, kLabelW, kRowH - 2,
            hwnd_, nullptr, hInst, nullptr);

        const int ctrlX = kLabelW + kPadX * 2;
        const HMENU ctrlMenu =
            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(e.controlId));

        switch (pi.type) {
            case LV2Plugin::PortInfo::ControlType::Float:
                if (pi.isEnum && !pi.scalePoints.empty()) {
                    // Enumeration → combobox
                    e.controlHwnd = CreateWindowExA(
                        0, "COMBOBOX", nullptr,
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                        ctrlX, y, kCtrlW, kRowH * 6,
                        hwnd_, ctrlMenu, hInst, nullptr);
                    if (e.controlHwnd) {
                        for (const auto& sp : pi.scalePoints) {
                            SendMessageA(e.controlHwnd, CB_ADDSTRING, 0,
                                         reinterpret_cast<LPARAM>(sp.second.c_str()));
                            e.enumValues.push_back(sp.first);
                        }
                        // Select the item whose float value is closest to current
                        int best = 0;
                        float bestDist = std::fabs(e.enumValues[0] - pi.currentVal);
                        for (int k = 1; k < (int)e.enumValues.size(); ++k) {
                            float d = std::fabs(e.enumValues[k] - pi.currentVal);
                            if (d < bestDist) { bestDist = d; best = k; }
                        }
                        SendMessage(e.controlHwnd, CB_SETCURSEL,
                                    static_cast<WPARAM>(best), 0);
                    }
                } else {
                    // Continuous float → trackbar
                    e.controlHwnd = CreateWindowExA(
                        0, TRACKBAR_CLASSA, nullptr,
                        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                        ctrlX, y, kCtrlW, kRowH - 2,
                        hwnd_, ctrlMenu, hInst, nullptr);
                    if (e.controlHwnd) {
                        SendMessage(e.controlHwnd, TBM_SETRANGEMIN, FALSE, 0);
                        SendMessage(e.controlHwnd, TBM_SETRANGEMAX, FALSE, 10000);
                        const float range = e.maxVal - e.minVal;
                        int pos = (range > 0.0f)
                            ? static_cast<int>((pi.currentVal - e.minVal) / range * 10000.0f)
                            : 0;
                        pos = (std::max)(0, (std::min)(10000, pos));
                        SendMessage(e.controlHwnd, TBM_SETPOS,
                                    TRUE, static_cast<LPARAM>(pos));
                    }
                }
                break;

            case LV2Plugin::PortInfo::ControlType::Toggle:
                e.controlHwnd = CreateWindowExA(
                    0, "BUTTON", nullptr,
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    ctrlX, y, 20, kRowH - 2,
                    hwnd_, ctrlMenu, hInst, nullptr);
                if (e.controlHwnd)
                    SendMessage(e.controlHwnd, BM_SETCHECK,
                                pi.currentVal > 0.5f ? BST_CHECKED : BST_UNCHECKED, 0);
                break;

            case LV2Plugin::PortInfo::ControlType::Trigger:
                e.controlHwnd = CreateWindowExA(
                    0, "BUTTON", "Trigger",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    ctrlX, y, 80, kRowH - 2,
                    hwnd_, ctrlMenu, hInst, nullptr);
                break;

            case LV2Plugin::PortInfo::ControlType::AtomFilePath:
                e.controlHwnd = CreateWindowExA(
                    0, "BUTTON", "Browse...",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    ctrlX, y, 80, kRowH - 2,
                    hwnd_, ctrlMenu, hInst, nullptr);
                break;
        }

        y += kRowH + kPadY;
        entries_.push_back(std::move(e));
    }

    contentH_ = y;
    scrollY_  = 0;
    updateScrollInfo();
    InvalidateRect(hwnd_, nullptr, TRUE);
    return true;
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void ParameterPanel::clear() {
    for (auto& e : entries_) {
        if (e.labelHwnd)   { DestroyWindow(e.labelHwnd);   e.labelHwnd   = nullptr; }
        if (e.controlHwnd) { DestroyWindow(e.controlHwnd); e.controlHwnd = nullptr; }
    }
    entries_.clear();
    scrollY_  = 0;
    contentH_ = 0;

    if (hwnd_) {
        SetScrollPos(hwnd_, SB_VERT, 0, TRUE);
        ShowWindow(hwnd_, SW_HIDE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ParameterPanel::resize(const RECT& bounds) {
    if (!hwnd_) return;
    MoveWindow(hwnd_,
               bounds.left, bounds.top,
               bounds.right  - bounds.left,
               bounds.bottom - bounds.top,
               TRUE);
    updateScrollInfo();
}

HWND ParameterPanel::hwnd() const { return hwnd_; }

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

void ParameterPanel::updateScrollInfo() {
    if (!hwnd_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int viewH = rc.bottom - rc.top;

    SCROLLINFO si  = {};
    si.cbSize      = sizeof(si);
    si.fMask       = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin        = 0;
    si.nMax        = (contentH_ > 0) ? (contentH_ - 1) : 0;
    si.nPage       = static_cast<UINT>((std::max)(1, viewH));
    si.nPos        = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);

    // Read back the clamped position Windows chose.
    si.fMask = SIF_POS;
    GetScrollInfo(hwnd_, SB_VERT, &si);
    scrollBy(si.nPos - scrollY_);
}

void ParameterPanel::scrollBy(int dy) {
    if (dy == 0) return;
    scrollY_ += dy;
    ScrollWindowEx(hwnd_, 0, -dy, nullptr, nullptr,
                   nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
}

void ParameterPanel::onVScroll(UINT code, int thumbPos) {
    SCROLLINFO si = {};
    si.cbSize     = sizeof(si);
    si.fMask      = SIF_ALL;
    GetScrollInfo(hwnd_, SB_VERT, &si);

    int newPos = si.nPos;
    switch (code) {
        case SB_TOP:           newPos = si.nMin; break;
        case SB_BOTTOM:        newPos = si.nMax; break;
        case SB_LINEUP:        newPos -= kRowH; break;
        case SB_LINEDOWN:      newPos += kRowH; break;
        case SB_PAGEUP:        newPos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN:      newPos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: newPos = thumbPos; break;
        default: break;
    }

    const int maxPos = static_cast<int>(si.nMax) - static_cast<int>(si.nPage) + 1;
    newPos = (std::max)(si.nMin, (std::min)(newPos, maxPos));
    const int delta = newPos - scrollY_;
    if (delta != 0) {
        SetScrollPos(hwnd_, SB_VERT, newPos, TRUE);
        scrollBy(delta);
    }
}

void ParameterPanel::onMouseWheel(int delta) {
    // Three rows per wheel notch
    const int lines = -delta / WHEEL_DELTA * kRowH * 3;
    onVScroll(SB_THUMBTRACK, scrollY_ + lines);
}

// ---------------------------------------------------------------------------
// Control event handlers
// ---------------------------------------------------------------------------

void ParameterPanel::onHScroll(HWND bar, UINT /*code*/) {
    for (const auto& e : entries_) {
        if (e.controlHwnd != bar) continue;
        if (e.type != LV2Plugin::PortInfo::ControlType::Float) break;

        const int   pos   = static_cast<int>(SendMessage(bar, TBM_GETPOS, 0, 0));
        const float range = e.maxVal - e.minVal;
        const float value = (range > 0.0f)
            ? e.minVal + (static_cast<float>(pos) / 10000.0f) * range
            : e.minVal;
        engine_->setValue(slot_, e.portIndex, value);
        break;
    }
}

void ParameterPanel::onCommand(UINT id, UINT notif, HWND ctrl) {
    for (const auto& e : entries_) {
        if (e.controlId != id) continue;

        switch (e.type) {
            case LV2Plugin::PortInfo::ControlType::Toggle:
                if (notif == BN_CLICKED) {
                    const bool checked =
                        (SendMessage(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    engine_->setValue(slot_, e.portIndex, checked ? 1.0f : 0.0f);
                }
                break;

            case LV2Plugin::PortInfo::ControlType::Trigger:
                if (notif == BN_CLICKED)
                    engine_->setValue(slot_, e.portIndex, 1.0f);
                break;

            case LV2Plugin::PortInfo::ControlType::Float:
                // Only combobox (enum) variants produce WM_COMMAND
                if (notif == CBN_SELCHANGE) {
                    const int sel = static_cast<int>(
                        SendMessage(ctrl, CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(e.enumValues.size()))
                        engine_->setValue(slot_, e.portIndex, e.enumValues[sel]);
                }
                break;

            case LV2Plugin::PortInfo::ControlType::AtomFilePath:
                if (notif == BN_CLICKED) {
                    char path[MAX_PATH] = {};
                    OPENFILENAMEA ofn   = {};
                    ofn.lStructSize     = sizeof(ofn);
                    ofn.hwndOwner       = hwnd_;
                    ofn.lpstrFile       = path;
                    ofn.nMaxFile        = MAX_PATH;
                    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameA(&ofn))
                        engine_->setFilePath(slot_, e.writableUri, path);
                }
                break;
        }
        break;
    }
}
