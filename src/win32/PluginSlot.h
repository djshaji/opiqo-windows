#pragma once

#include <windows.h>
#include "ParameterPanel.h"

class LiveEffectEngine;

class PluginSlot {
public:
    // Register the "OpiqoPluginSlot" window class. Call once at app startup.
    static bool registerClass(HINSTANCE hInst);

    bool create(HWND parent, int slotIndex, const RECT& bounds);
    HWND hwnd() const;

    // Returns true if a plugin is currently loaded in this slot.
    bool hasPlugin() const { return bypassButton_ && IsWindowEnabled(bypassButton_); }

    // Set label to active plugin name and enable Bypass/Delete.
    void setPlugin(const char* name);

    // Reset label to "Empty Slot" and disable Bypass/Delete.
    void clearPlugin();

    // Update the Bypass button text to reflect current bypass state.
    void setBypassVisual(bool bypassed);

    // Reposition and resize the slot panel and its buttons.
    void resize(const RECT& bounds);

    // Build dynamic parameter controls for the plugin currently in this slot.
    // Call after addPlugin() succeeds.  engine must remain valid for the panel's
    // lifetime.
    bool buildParameterPanel(LiveEffectEngine* engine);

    // Destroy all dynamic parameter controls.
    // Call before clearPlugin() or on delete.
    void clearParameterPanel();

private:
    static LRESULT CALLBACK SlotWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam);

    HWND           hwnd_         = nullptr;
    HWND           labelStatic_  = nullptr;
    HWND           addButton_    = nullptr;
    HWND           bypassButton_ = nullptr;
    HWND           deleteButton_ = nullptr;
    int            slotIndex_    = 0;
    ParameterPanel paramPanel_;
};
