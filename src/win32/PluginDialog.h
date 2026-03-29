#pragma once

#include <string>
#include <windows.h>

class PluginDialog {
public:
    static bool showModal(HWND parent, std::string* selectedUri);
};
