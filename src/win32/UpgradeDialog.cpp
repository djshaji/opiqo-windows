#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "UpgradeDialog.h"
#include "resource.h"
#include "totp.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// License key format: 6 digit number (TOTP code) 
// ---------------------------------------------------------------------------

static bool validateKeyFormat(const char* key) {
    // Must be exactly 6 decimal digits.
    if (strlen(key) != 6) return false;
    for (int i = 0; i < 6; ++i)
        if (!isdigit((unsigned char)key[i])) return false;
    return true;
}

static bool validateKeyTotp(const char* key) {
    uint32_t code = static_cast<uint32_t>(strtoul(key, nullptr, 10));
    return totp::verify(BASE32_SECRET, code);
}

// ---------------------------------------------------------------------------
// Dialog procedure
// ---------------------------------------------------------------------------

static INT_PTR CALLBACK UpgradeDialogProc(HWND dlg, UINT msg,
                                           WPARAM wParam, LPARAM lParam) {
    AppSettings* settings = reinterpret_cast<AppSettings*>(
        GetWindowLongPtr(dlg, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtr(dlg, DWLP_USER, lParam);
            // Pre-fill key if one was previously entered.
            if (lParam) {
                AppSettings* s = reinterpret_cast<AppSettings*>(lParam);
                if (!s->licenseKey.empty())
                    SetDlgItemTextA(dlg, IDC_LICENSE_KEY, s->licenseKey.c_str());
            }
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_UPGRADE_LINK:
                    ShellExecuteA(dlg, "open",
                        "https://opiqo.acoustixaudio.org/",
                        nullptr, nullptr, SW_SHOWNORMAL);
                    return TRUE;

                case IDC_LICENSE_ACTIVATE: {
                    char raw[64] = {};
                    GetDlgItemTextA(dlg, IDC_LICENSE_KEY, raw, sizeof(raw));

                    // Trim leading/trailing whitespace.
                    char* key = raw;
                    while (*key == ' ' || *key == '\t') ++key;
                    size_t klen = strlen(key);
                    while (klen > 0 &&
                           (key[klen - 1] == ' ' || key[klen - 1] == '\t'))
                        key[--klen] = '\0';

                    if (!validateKeyFormat(key)) {
                        SetDlgItemTextA(dlg, IDC_UPGRADE_STATUS,
                            "Invalid format. Enter the 6-digit license code.");
                        return TRUE;
                    }
                    if (!validateKeyTotp(key)) {
                        SetDlgItemTextA(dlg, IDC_UPGRADE_STATUS,
                            "License key is not valid. Please check and try again.");
                        return TRUE;
                    }

                    // Successfully validated — persist and close.
                    settings->licenseKey = key;
                    settings->activated  = true;
                    settings->save();
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool UpgradeDialog::show(HWND parent, AppSettings* settings) {
    INT_PTR result = DialogBoxParamA(
        GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(IDD_UPGRADE),
        parent,
        UpgradeDialogProc,
        reinterpret_cast<LPARAM>(settings));
    return (result == IDOK);
}
