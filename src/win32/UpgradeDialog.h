#pragma once

#include <windows.h>
#include "AppSettings.h"

class UpgradeDialog {
public:
    // Shows the Upgrade to Pro dialog.
    // Returns true if the user successfully activated a valid license key.
    static bool show(HWND parent, AppSettings* settings);
};
