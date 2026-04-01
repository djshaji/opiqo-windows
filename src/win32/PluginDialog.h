#pragma once

#include <string>
#include <windows.h>

class LiveEffectEngine;

class PluginDialog {
public:
    // Opens a modal plugin picker populated from engine->getAvailablePlugins().
    // Returns true and sets *selectedUri if the user confirmed a selection.
    // Returns false if the user cancelled or no plugins are available.
    static bool showModal(HWND parent, LiveEffectEngine* engine,
                          std::string* selectedUri);
};
