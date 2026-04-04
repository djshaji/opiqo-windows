#include "SettingsDialog.h"
#include "resource.h"

#include <cstdio>
#include <vector>
#include <string>

struct SettingsContext {
    AppSettings*              settings;
    WasapiDeviceEnum*         deviceEnum;
    std::vector<std::string>  inputIds;
    std::vector<std::string>  outputIds;
};

static INT_PTR CALLBACK SettingsDialogProc(HWND dlg, UINT msg,
                                           WPARAM wParam, LPARAM lParam) {
    SettingsContext* ctx = reinterpret_cast<SettingsContext*>(
        GetWindowLongPtr(dlg, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<SettingsContext*>(lParam);
            SetWindowLongPtr(dlg, DWLP_USER, lParam);

            // --- Sample rate combo ---
            // Index 0 = "Auto": let the audio driver decide the rate.
            // A saved sampleRate of 0 means Auto.
            static const int kSampleRates[] = { 44100, 48000, 96000 };
            HWND srCombo = GetDlgItem(dlg, IDC_SETTINGS_SAMPLERATE);
            SendMessageA(srCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Auto"));
            int  srSel   = 0; // default: Auto
            for (int i = 0; i < 3; ++i) {
                char buf[12];
                snprintf(buf, sizeof(buf), "%d", kSampleRates[i]);
                SendMessageA(srCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
                if (ctx->settings->sampleRate != 0 &&
                    kSampleRates[i] == ctx->settings->sampleRate) srSel = i + 1;
            }
            SendMessageA(srCombo, CB_SETCURSEL, srSel, 0);

            // --- Block size combo ---
            static const int kBlockSizes[] = { 256, 512, 1024, 2048, 4096 };
            HWND bsCombo = GetDlgItem(dlg, IDC_SETTINGS_BLOCKSIZE);
            int  bsSel   = 1; // default: 512
            for (int i = 0; i < 5; ++i) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", kBlockSizes[i]);
                SendMessageA(bsCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(buf));
                if (kBlockSizes[i] == ctx->settings->blockSize) bsSel = i;
            }
            SendMessageA(bsCombo, CB_SETCURSEL, bsSel, 0);

            // --- Exclusive mode checkbox ---
            CheckDlgButton(dlg, IDC_SETTINGS_EXCLUSIVE,
                           ctx->settings->exclusiveMode ? BST_CHECKED : BST_UNCHECKED);

            // --- Input device combo ---
            auto inputs  = ctx->deviceEnum->enumerateInputDevices();
            HWND inCombo = GetDlgItem(dlg, IDC_SETTINGS_INPUT);
            int  inSel   = 0;
            ctx->inputIds.clear();
            for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
                ctx->inputIds.push_back(inputs[i].id);
                SendMessageA(inCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(inputs[i].friendlyName.c_str()));
                if (inputs[i].id == ctx->settings->inputDeviceId) inSel = i;
            }
            SendMessageA(inCombo, CB_SETCURSEL, inSel, 0);

            // --- Output device combo ---
            auto outputs  = ctx->deviceEnum->enumerateOutputDevices();
            HWND outCombo = GetDlgItem(dlg, IDC_SETTINGS_OUTPUT);
            int  outSel   = 0;
            ctx->outputIds.clear();
            for (int i = 0; i < static_cast<int>(outputs.size()); ++i) {
                ctx->outputIds.push_back(outputs[i].id);
                SendMessageA(outCombo, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(outputs[i].friendlyName.c_str()));
                if (outputs[i].id == ctx->settings->outputDeviceId) outSel = i;
            }
            SendMessageA(outCombo, CB_SETCURSEL, outSel, 0);

            return TRUE;
        }

        case WM_COMMAND:
            if (!ctx) break;
            switch (LOWORD(wParam)) {
                case IDOK: {
                    static const int kSampleRates[] = { 44100, 48000, 96000 };
                    static const int kBlockSizes[]  = { 256, 512, 1024, 2048, 4096 };

                    int srIdx  = static_cast<int>(
                        SendDlgItemMessageA(dlg, IDC_SETTINGS_SAMPLERATE, CB_GETCURSEL, 0, 0));
                    int bsIdx  = static_cast<int>(
                        SendDlgItemMessageA(dlg, IDC_SETTINGS_BLOCKSIZE,  CB_GETCURSEL, 0, 0));
                    int inIdx  = static_cast<int>(
                        SendDlgItemMessageA(dlg, IDC_SETTINGS_INPUT,      CB_GETCURSEL, 0, 0));
                    int outIdx = static_cast<int>(
                        SendDlgItemMessageA(dlg, IDC_SETTINGS_OUTPUT,     CB_GETCURSEL, 0, 0));
                    bool excl  = (IsDlgButtonChecked(dlg, IDC_SETTINGS_EXCLUSIVE) == BST_CHECKED);

                    if (srIdx == 0)
                        ctx->settings->sampleRate = 0;  // Auto
                    else if (srIdx >= 1 && srIdx <= 3)
                        ctx->settings->sampleRate = kSampleRates[srIdx - 1];
                    if (bsIdx  >= 0 && bsIdx  < 5) ctx->settings->blockSize  = kBlockSizes[bsIdx];
                    if (inIdx  >= 0 && inIdx  < static_cast<int>(ctx->inputIds.size()))
                        ctx->settings->inputDeviceId  = ctx->inputIds[inIdx];
                    if (outIdx >= 0 && outIdx < static_cast<int>(ctx->outputIds.size()))
                        ctx->settings->outputDeviceId = ctx->outputIds[outIdx];
                    ctx->settings->exclusiveMode = excl;

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

bool SettingsDialog::show(HWND parent, AppSettings* settings, WasapiDeviceEnum* deviceEnum) {
    SettingsContext ctx;
    ctx.settings   = settings;
    ctx.deviceEnum = deviceEnum;

    INT_PTR result = DialogBoxParamA(
        GetModuleHandleA(nullptr),
        MAKEINTRESOURCEA(IDD_SETTINGS),
        parent,
        SettingsDialogProc,
        reinterpret_cast<LPARAM>(&ctx));

    return (result == IDOK);
}
