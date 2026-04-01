#pragma once

#include <windows.h>
#include "AppSettings.h"
#include "WasapiDeviceEnum.h"

class SettingsDialog {
public:
    // Shows the settings dialog modally. Writes accepted values into *settings.
    // Returns true if the user clicked OK.
    static bool show(HWND parent, AppSettings* settings, WasapiDeviceEnum* deviceEnum);
};
