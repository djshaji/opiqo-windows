#pragma once

#include <string>
#include <vector>
#include <windows.h>
// logging_macros.h must be visible before LV2Plugin.hpp on non-Android builds.
#include "../logging_macros.h"
#include "../LV2Plugin.hpp"

class LiveEffectEngine;

// ---------------------------------------------------------------------------
// ParameterPanel
//
// A scrollable child window that dynamically creates Win32 controls for every
// control-input port of of an LV2 plugin.  One panel lives inside each
// PluginSlot and is rebuilt whenever the slot's plugin changes.
//
// Control mapping (per LV2Plugin::PortInfo::ControlType):
//   Float (plain)       -> TRACKBAR  (0-10000 integer, mapped to min-max range)
//   Float (enumeration) -> COMBOBOX  (CBS_DROPDOWNLIST with scale-point labels)
//   Toggle              -> CHECKBOX  (BS_AUTOCHECKBOX)
//   Trigger             -> BUTTON    (pushbutton, fires a single setValue(1.0))
//   AtomFilePath        -> BUTTON    (Browse... opens GetOpenFileNameA dialog)
//
// All value changes are forwarded directly to engine->setValue() or
// engine->setFilePath() on the UI thread – never from the audio thread.
// ---------------------------------------------------------------------------

class ParameterPanel {
public:
    // Register the "OpiqoParamPanel" window class.  Call once before any
    // ParameterPanel::build() call (e.g. in MainWindow::create()).
    static bool registerClass(HINSTANCE hInst);

    // Build (or rebuild) the panel as a child of |parent| for the given
    // |slot| (1-4).  |ports| is the PortInfo vector from getPluginPortInfo().
    // |baseId| is the first WM_COMMAND control ID to assign.
    // Destroys any existing controls before building the new set.
    bool build(HWND parent, LiveEffectEngine* engine, int slot,
               const std::vector<LV2Plugin::PortInfo>& ports, int baseId);

    // Destroy all dynamic controls and hide the panel window.
    // Does not destroy the panel HWND itself.
    void clear();

    // Reposition and resize the panel within the slot's client area.
    void resize(const RECT& bounds);

    HWND hwnd() const;

private:
    // Per-control bookkeeping needed to map WM_COMMAND/WM_HSCROLL back to
    // engine calls.
    struct ControlEntry {
        int         portIndex   = 0;
        LV2Plugin::PortInfo::ControlType type{};
        float       minVal      = 0.0f;
        float       maxVal      = 1.0f;
        std::string symbol;
        std::string writableUri;
        std::vector<float> enumValues;  // parallel to combobox items
        HWND  labelHwnd         = nullptr;
        HWND  controlHwnd       = nullptr;
        UINT  controlId         = 0;
    };

    static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Message handlers
    void onHScroll(HWND bar, UINT code);
    void onCommand(UINT id, UINT notif, HWND ctrl);
    void onVScroll(UINT code, int thumbPos);
    void onMouseWheel(int delta);

    // Scrolling helpers
    void updateScrollInfo();
    void scrollBy(int dy);

    HWND              hwnd_     = nullptr;
    LiveEffectEngine* engine_   = nullptr;
    int               slot_     = 0;
    int               baseId_   = 0;
    int               scrollY_  = 0;   // current vertical scroll offset (px)
    int               contentH_ = 0;   // total height of all rows (px)

    std::vector<ControlEntry> entries_;
};
