#include "PluginDialog.h"
#include "resource.h"
#include "../LiveEffectEngine.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Internal modal dialog state (passed via GWLP_USERDATA)
// ---------------------------------------------------------------------------
struct PluginDialogState {
    // Parallel arrays: URI and display name for each list entry.
    std::vector<std::string> uris;
    std::vector<std::string> names;
    std::string*             selectedUri = nullptr;
    bool                     confirmed   = false;
    HWND                     listBox     = nullptr;
    HFONT                    font        = nullptr;
};

static LRESULT CALLBACK PluginDlgWndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    PluginDialogState* state = reinterpret_cast<PluginDialogState*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            state = reinterpret_cast<PluginDialogState*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA,
                             reinterpret_cast<LONG_PTR>(state));

            HINSTANCE hInst = GetModuleHandle(nullptr);
            int w = cs->cx, h = cs->cy;

            // ListBox fills most of the dialog.
            state->listBox = CreateWindowExA(
                WS_EX_CLIENTEDGE, "LISTBOX", nullptr,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_SORT,
                8, 8, w - 16, h - 52,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(IDC_PLUGIN_LIST)),
                hInst, nullptr);

            for (const auto& name : state->names)
                SendMessageA(state->listBox, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(name.c_str()));

            // OK / Cancel buttons.
            CreateWindowExA(0, "BUTTON", "OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                w - 176, h - 36, 80, 26,
                hwnd, reinterpret_cast<HMENU>(IDOK), hInst, nullptr);

            CreateWindowExA(0, "BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                w - 88, h - 36, 80, 26,
                hwnd, reinterpret_cast<HMENU>(IDCANCEL), hInst, nullptr);

            // Apply the shared UI font to all child controls.
            if (state->font) {
                EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
                    SendMessage(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(state->font));
            }

            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDOK || (id == IDC_PLUGIN_LIST &&
                               HIWORD(wParam) == LBN_DBLCLK)) {
                // Resolve the selected item back to a URI.
                int sel = static_cast<int>(
                    SendMessage(state->listBox, LB_GETCURSEL, 0, 0));
                if (sel == LB_ERR) break;  // Nothing selected.

                // Find the URI matching the display name at position sel.
                // LBS_SORT re-orders items, so retrieve the text and scan.
                char buf[512] = {};
                SendMessageA(state->listBox, LB_GETTEXT, sel,
                             reinterpret_cast<LPARAM>(buf));
                std::string selectedName(buf);
                for (size_t i = 0; i < state->names.size(); ++i) {
                    if (state->names[i] == selectedName) {
                        *state->selectedUri = state->uris[i];
                        break;
                    }
                }
                state->confirmed = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (id == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool PluginDialog::showModal(HWND parent, LiveEffectEngine* engine,
                             std::string* selectedUri, HFONT font) {
    if (!engine || !selectedUri) return false;

    json all = engine->getAvailablePlugins();
    if (all.empty()) {
        MessageBoxA(parent, "No LV2 plugins found.",
                    "Plugin Browser", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    PluginDialogState state;
    state.selectedUri = selectedUri;
    state.font        = font;
    for (auto it = all.begin(); it != all.end(); ++it) {
        state.uris.push_back(it.key());
        std::string name = it.key(); // fallback
        if (it.value().contains("name") && it.value()["name"].is_string())
            name = it.value()["name"].get<std::string>();
        state.names.push_back(name);
    }

    // Register a lightweight window class for the dialog.
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc   = PluginDlgWndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.lpszClassName = "OpiqoPluginDlg";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassA(&wc);
        classRegistered = true;
    }

    // Disable parent while modal is open.
    EnableWindow(parent, FALSE);

    HWND dlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "OpiqoPluginDlg", "Choose Plugin",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 480, 360,
        parent, nullptr, GetModuleHandle(nullptr),
        &state);

    if (dlg) {
        // Centre over parent.
        RECT pr, dr;
        GetWindowRect(parent, &pr);
        GetWindowRect(dlg, &dr);
        int x = pr.left + (pr.right  - pr.left  - (dr.right  - dr.left))  / 2;
        int y = pr.top  + (pr.bottom - pr.top   - (dr.bottom - dr.top))   / 2;
        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);

        // Local message loop.
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    return state.confirmed;
}
