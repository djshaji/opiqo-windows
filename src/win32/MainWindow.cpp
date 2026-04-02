#include "MainWindow.h"

#include <commctrl.h>
#include <fcntl.h>
#include <fstream>
#include <io.h>
#include <string>
#include "resource.h"
#include "../FileWriter.h"
#include "SettingsDialog.h"

// Posted from the COM notification thread; handled on the UI thread.
static constexpr UINT WM_OPIQO_DEVICE_CHANGE = WM_APP + 1;

static constexpr int kBarHeight    = 40;   // ControlBar height (px)
static constexpr int kStatusHeight = 22;   // Status bar height (px) — self-sized by STATUSCLASSNAME
static constexpr int kMinWidth     = 900;
static constexpr int kMinHeight    = 650;

MainWindow::MainWindow(HINSTANCE instance)
    : instance_(instance) {}

MainWindow::~MainWindow() = default;

bool MainWindow::create(int nCmdShow) {
    // Initialise common controls (required for TRACKBAR_CLASS used in
    // ParameterPanel).
    INITCOMMONCONTROLSEX iccex = {};
    iccex.dwSize = sizeof(iccex);
    iccex.dwICC  = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&iccex);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return false;

    settings_ = AppSettings::load();

    const char* className = "OpiqoMainWindow";

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = MainWindow::WndProc;
    wc.hInstance     = instance_;
    wc.lpszClassName = className;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    if (RegisterClassA(&wc) == 0) {
        CoUninitialize();
        return false;
    }

    PluginSlot::registerClass(instance_);
    ParameterPanel::registerClass(instance_);

    hwnd_ = CreateWindowExA(
        0,
        className,
        "Opiqo Windows Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kMinWidth,
        kMinHeight,
        nullptr,
        LoadMenuA(instance_, MAKEINTRESOURCEA(IDR_MAINMENU)),
        instance_,
        this);

    if (hwnd_ == nullptr) {
        CoUninitialize();
        return false;
    }

    // Enumerate devices and wire hot-plug notifications.
    deviceEnum_ = std::make_unique<WasapiDeviceEnum>();
    deviceEnum_->setChangeCallback([hwnd = hwnd_] {
        PostMessage(hwnd, WM_OPIQO_DEVICE_CHANGE, 0, 0);
    });

    // Resolve saved device ids against what is actually present.
    onDeviceListChanged();

    // Initialise LV2 plugin discovery. The path is resolved relative to the
    // executable; on Windows the bundles live in bin\lv2 next to the .exe.
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string lv2Dir = exePath;
        auto sep = lv2Dir.rfind('\\');
        if (sep != std::string::npos) lv2Dir.resize(sep);
        lv2Dir += "\\lv2";
        liveEngine_.initPlugins(lv2Dir);
    }

    // Wire the DSP engine into the audio pipeline.
    audioEngine_.setEngine(&liveEngine_);

    // Status bar along the bottom of the client area.
    // STATUSCLASSNAME docks itself to the bottom automatically when it
    // receives WM_SIZE; doLayout() triggers this by sending WM_SIZE to it.
    statusBar_ = CreateWindowExA(
        0, STATUSCLASSNAME, nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0,
        hwnd_, nullptr, instance_, nullptr);
    if (statusBar_) {
        // Two equal-width parts: input device | output device.
        int parts[2] = { 300, -1 };
        SendMessage(statusBar_, SB_SETPARTS, 2,
                    reinterpret_cast<LPARAM>(parts));
        SendMessageA(statusBar_, SB_SETTEXTA, 0,
                     reinterpret_cast<LPARAM>("In: (none)"));
        SendMessageA(statusBar_, SB_SETTEXTA, 1,
                     reinterpret_cast<LPARAM>("Out: (none)"));
    }

    // Create the control bar and the 2x2 slot grid (bounds set by doLayout).
    {
        RECT dummy = {};
        controlBar_.create(hwnd_, dummy);
        // Forward WM_HSCROLL from the ControlBar container to this window.
        SetWindowSubclass(controlBar_.hwnd(), ControlBarSubclassProc,
                          1 /*subclassId*/, reinterpret_cast<DWORD_PTR>(this));
        controlBar_.setFormatIndex(settings_.recordFormat);
        controlBar_.showQualityCombo(settings_.recordFormat != 0);
        controlBar_.enableRecordButton(false); // enabled when engine reaches Running
        // Sync gain slider and engine to the persisted setting.
        if (liveEngine_.gain)
            *liveEngine_.gain = settings_.gain;
        controlBar_.setGainValue(static_cast<int>(settings_.gain * 100.0f + 0.5f));
        for (int i = 0; i < 4; ++i)
            slots_[i].create(hwnd_, i, dummy);
    }

    // Apply initial layout.
    doLayout();

    ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void MainWindow::doLayout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int totalW = rc.right;
    int totalH = rc.bottom;

    // Status bar: STATUSCLASSNAME docks itself to the bottom of the parent
    // when it receives WM_SIZE. Send WM_SIZE explicitly since we handle the
    // parent's WM_SIZE ourselves (without calling DefWindowProc on the parent).
    int sbH = kStatusHeight;
    if (statusBar_) {
        SendMessage(statusBar_, WM_SIZE, 0, 0);
        // Update part widths so the right half stretches to the window edge.
        int parts[2] = { totalW / 2, -1 };
        SendMessage(statusBar_, SB_SETPARTS, 2,
                    reinterpret_cast<LPARAM>(parts));
        // Read back the actual height chosen by STATUSCLASSNAME (theme/DPI-aware).
        RECT sbrc = {};
        GetWindowRect(statusBar_, &sbrc);
        int actualH = sbrc.bottom - sbrc.top;
        if (actualH > 0) sbH = actualH;
    }

    // Slot grid: full width, from top of client area to where the control bar starts.
    int slotAreaTop = 0;
    int slotAreaBot = totalH - kBarHeight - sbH;
    if (slotAreaBot < slotAreaTop) slotAreaBot = slotAreaTop; // guard: very small window
    int slotAreaH = slotAreaBot - slotAreaTop;
    int halfW = totalW / 2;
    int halfH = slotAreaH / 2;

    RECT slotBounds[4] = {
        { 0,     slotAreaTop,          halfW,  slotAreaTop + halfH }, // Slot 1
        { halfW, slotAreaTop,          totalW, slotAreaTop + halfH }, // Slot 2
        { 0,     slotAreaTop + halfH,  halfW,  slotAreaBot },          // Slot 3
        { halfW, slotAreaTop + halfH,  totalW, slotAreaBot },          // Slot 4
    };
    for (int i = 0; i < 4; ++i)
        slots_[i].resize(slotBounds[i]);

    // Control bar: sits directly above the status bar.
    RECT barBounds = { 0, slotAreaBot, totalW, slotAreaBot + kBarHeight };
    controlBar_.resize(barBounds);
}

void MainWindow::onEngineStatePoll() {
    AudioEngine::State s = audioEngine_.state();
    if (s == AudioEngine::State::Starting)
        return;  // Still transitioning — keep polling.

    KillTimer(hwnd_, IDT_ENGINE_STATE);

    if (s == AudioEngine::State::Running) {
        // Successfully started — leave toggle ON.
        controlBar_.setPowerState(true);
        controlBar_.enableRecordButton(true);
        // Start a long-period watchdog to detect mid-session device loss.
        SetTimer(hwnd_, IDT_ENGINE_WATCHDOG, 500, nullptr);
    } else {
        // Error or unexpected state — revert toggle and report.
        controlBar_.setPowerState(false);
        controlBar_.enableRecordButton(false);
        std::string msg = audioEngine_.errorMessage();
        if (msg.empty()) msg = "Audio engine failed to start.";
        MessageBoxA(hwnd_, msg.c_str(),
                    "Opiqo \u2014 Engine Error", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::onDeviceListChanged() {
    auto inputs  = deviceEnum_->enumerateInputDevices();
    auto outputs = deviceEnum_->enumerateOutputDevices();

    settings_.inputDeviceId  = WasapiDeviceEnum::resolveOrDefault(
        inputs,  settings_.inputDeviceId);
    settings_.outputDeviceId = WasapiDeviceEnum::resolveOrDefault(
        outputs, settings_.outputDeviceId);

    // Update status bar with friendly names.
    if (statusBar_) {
        std::string inName  = "In: (none)";
        std::string outName = "Out: (none)";
        for (const auto& d : inputs)
            if (d.id == settings_.inputDeviceId) { inName  = "In: "  + d.friendlyName; break; }
        for (const auto& d : outputs)
            if (d.id == settings_.outputDeviceId) { outName = "Out: " + d.friendlyName; break; }
        SendMessageA(statusBar_, SB_SETTEXTA, 0,
                     reinterpret_cast<LPARAM>(inName.c_str()));
        SendMessageA(statusBar_, SB_SETTEXTA, 1,
                     reinterpret_cast<LPARAM>(outName.c_str()));
    }

    // If the engine dropped into Error (device pulled), trigger recovery.
    if (audioEngine_.state() == AudioEngine::State::Error) {
        KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);
        onEngineError();
    }
}

void MainWindow::onEngineError() {
    // Stop any active recording before interacting with the user.
    if (recordingFd_ >= 0) {
        liveEngine_.stopRecording();
        _close(recordingFd_);
        recordingFd_ = -1;
        controlBar_.setRecordState(false);
    }

    controlBar_.setPowerState(false);
    controlBar_.enableRecordButton(false);

    // Show the error in the input status bar pane.
    std::string errMsg = audioEngine_.errorMessage();
    if (errMsg.empty()) errMsg = "Audio device lost.";
    if (statusBar_)
        SendMessageA(statusBar_, SB_SETTEXTA, 0,
                     reinterpret_cast<LPARAM>(("Error: " + errMsg).c_str()));

    // Offer automatic restart on the resolved default device.
    int choice = MessageBoxA(hwnd_,
        (errMsg + "\n\nAttempt to restart with the default device?").c_str(),
        "Opiqo \u2014 Audio Error", MB_YESNO | MB_ICONWARNING);

    if (choice == IDYES) {
        onDeviceListChanged();  // Resolve saved IDs against current device list.
        bool launched = audioEngine_.start(
            settings_.sampleRate,
            settings_.blockSize,
            settings_.inputDeviceId,
            settings_.outputDeviceId,
            settings_.exclusiveMode);
        if (!launched) {
            controlBar_.setPowerState(false);
            MessageBoxA(hwnd_, audioEngine_.errorMessage().c_str(),
                        "Opiqo \u2014 Engine Error", MB_OK | MB_ICONERROR);
        } else {
            controlBar_.setPowerState(true);
            SetTimer(hwnd_, IDT_ENGINE_STATE, 50, nullptr);
        }
    }
}

LRESULT CALLBACK MainWindow::ControlBarSubclassProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR /*subclassId*/, DWORD_PTR refData) {
    if (msg == WM_HSCROLL) {
        MainWindow* self = reinterpret_cast<MainWindow*>(refData);
        if (self)
            self->onGainChanged();
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MainWindow::onGainChanged() {
    if (!liveEngine_.gain) return;
    int pos = controlBar_.gainValue();           // [0, 100]
    float g = static_cast<float>(pos) / 100.0f;
    *liveEngine_.gain = g;
    settings_.gain    = g;  // flushed to disk by WM_DESTROY / settings_.save()
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT message,
                                     WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr)
        return self->handleMessage(message, wParam, lParam);

    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_FILE_EXIT:
                    PostMessage(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
                case IDM_HELP_ABOUT:
                    MessageBoxA(hwnd_,
                        "Opiqo Windows Host\nVersion 1.0"
                        "\n\nLV2 audio plugin host with WASAPI duplex audio.",
                        "About Opiqo", MB_OK | MB_ICONINFORMATION);
                    return 0;
                case IDM_FILE_EXPORT_PRESET: {
                    char filePath[MAX_PATH] = {};
                    OPENFILENAMEA ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = hwnd_;
                    ofn.lpstrFilter = "Opiqo Preset\0*.json\0All Files\0*.*\0\0";
                    ofn.lpstrFile   = filePath;
                    ofn.nMaxFile    = MAX_PATH;
                    ofn.lpstrDefExt = "json";
                    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                    if (GetSaveFileNameA(&ofn)) {
                        std::string data = liveEngine_.getPresetList();
                        std::ofstream f(filePath);
                        if (f.is_open())
                            f << data;
                        else
                            MessageBoxA(hwnd_, "Failed to write preset file.",
                                        "Opiqo — Export", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                case IDM_FILE_IMPORT_PRESET: {
                    char filePath[MAX_PATH] = {};
                    OPENFILENAMEA ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = hwnd_;
                    ofn.lpstrFilter = "Opiqo Preset\0*.json\0All Files\0*.*\0\0";
                    ofn.lpstrFile   = filePath;
                    ofn.nMaxFile    = MAX_PATH;
                    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameA(&ofn)) {
                        std::ifstream f(filePath);
                        if (!f.is_open()) {
                            MessageBoxA(hwnd_, "Failed to open preset file.",
                                        "Opiqo — Import", MB_OK | MB_ICONERROR);
                            return 0;
                        }
                        try {
                            json preset;
                            f >> preset;
                            if (preset.contains("gain") && preset["gain"].is_number()
                                    && liveEngine_.gain) {
                                *liveEngine_.gain = preset["gain"].get<float>();
                                float g = *liveEngine_.gain;
                                settings_.gain = g;
                                controlBar_.setGainValue(
                                    static_cast<int>(g * 100.0f + 0.5f));
                            }
                            json all = liveEngine_.getAvailablePlugins();
                            for (int s = 1; s <= 4; ++s) {
                                std::string key = "plugin" + std::to_string(s);
                                if (!preset.contains(key) || !preset[key].is_object())
                                    continue;
                                const json& slotPreset = preset[key];
                                if (slotPreset.contains("uri") && slotPreset["uri"].is_string()) {
                                    std::string uri = slotPreset["uri"].get<std::string>();
                                    int result = liveEngine_.addPlugin(s, uri);
                                    if (result < 0) {
                                        MessageBoxA(hwnd_,
                                            ("Failed to load plugin for slot "
                                             + std::to_string(s) + ":\n" + uri).c_str(),
                                            "Opiqo \u2014 Import", MB_OK | MB_ICONWARNING);
                                        continue;
                                    }
                                    std::string name = uri;
                                    if (all.contains(uri) && all[uri].contains("name")
                                            && all[uri]["name"].is_string())
                                        name = all[uri]["name"].get<std::string>();
                                    slots_[s - 1].setPlugin(name.c_str());
                                    slotEnabled_[s - 1] = true;
                                }
                                liveEngine_.applyPreset(s, slotPreset);
                                slots_[s - 1].clearParameterPanel();
                                slots_[s - 1].buildParameterPanel(&liveEngine_);
                            }
                        } catch (...) {
                            MessageBoxA(hwnd_, "Preset file is corrupt or unreadable.",
                                        "Opiqo — Import", MB_OK | MB_ICONWARNING);
                        }
                    }
                    return 0;
                }
                case IDM_SETTINGS_OPEN: {
                    if (recordingFd_ >= 0) {
                        MessageBoxA(hwnd_,
                                    "Stop recording before changing audio settings.",
                                    "Opiqo", MB_OK | MB_ICONINFORMATION);
                        return 0;
                    }
                    AppSettings updated = settings_;
                    if (SettingsDialog::show(hwnd_, &updated, deviceEnum_.get())) {
                        bool wasRunning =
                            (audioEngine_.state() == AudioEngine::State::Running);
                        if (wasRunning) {
                            audioEngine_.stop();
                            KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);
                            controlBar_.setPowerState(false);
                            controlBar_.enableRecordButton(false);
                        }
                        settings_ = updated;
                        settings_.save();
                        onDeviceListChanged();
                        if (wasRunning) {
                            bool launched = audioEngine_.start(
                                settings_.sampleRate,
                                settings_.blockSize,
                                settings_.inputDeviceId,
                                settings_.outputDeviceId,
                                settings_.exclusiveMode);
                            if (!launched) {
                                controlBar_.setPowerState(false);
                                MessageBoxA(hwnd_,
                                            audioEngine_.errorMessage().c_str(),
                                            "Opiqo — Engine Error", MB_OK | MB_ICONERROR);
                            } else {
                                SetTimer(hwnd_, IDT_ENGINE_STATE, 50, nullptr);
                            }
                        }
                    }
                    return 0;
                }
                case IDC_POWER_TOGGLE: {
                    // Read the button's new checked state after the auto-toggle.
                    bool wantOn = (IsDlgButtonChecked(controlBar_.hwnd(),
                                                      IDC_POWER_TOGGLE) == BST_CHECKED);
                    if (wantOn) {
                        bool launched = audioEngine_.start(
                            settings_.sampleRate,
                            settings_.blockSize,
                            settings_.inputDeviceId,
                            settings_.outputDeviceId,
                            settings_.exclusiveMode);
                        if (!launched) {
                            // start() refused immediately (wrong state or bad params).
                            controlBar_.setPowerState(false);
                            MessageBoxA(hwnd_,
                                        audioEngine_.errorMessage().c_str(),
                                        "Opiqo — Engine Error", MB_OK | MB_ICONERROR);
                        } else {
                            // Poll until the audio thread settles to Running or Error.
                            SetTimer(hwnd_, IDT_ENGINE_STATE, 50, nullptr);
                        }
                    } else {
                        audioEngine_.stop();
                        // stop() is synchronous — state is Off when it returns.
                        KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);
                        controlBar_.setPowerState(false);
                        controlBar_.enableRecordButton(false);
                    }
                    return 0;
                }
                case IDC_RECORD_TOGGLE: {
                    bool wantRecord = (IsDlgButtonChecked(controlBar_.hwnd(),
                                                          IDC_RECORD_TOGGLE) == BST_CHECKED);
                    if (wantRecord) {
                        // Build per-format filter string and default extension.
                        static const FileType formatMap[] = {
                            FILE_TYPE_WAV, FILE_TYPE_MP3, FILE_TYPE_OGG
                        };
                        int fmtIdx = controlBar_.formatIndex();
                        const char* filter    = "WAV Files\0*.wav\0All Files\0*.*\0\0";
                        const char* defExt    = "wav";
                        if (fmtIdx == 1) { filter = "MP3 Files\0*.mp3\0All Files\0*.*\0\0"; defExt = "mp3"; }
                        else if (fmtIdx == 2) { filter = "OGG Files\0*.ogg\0All Files\0*.*\0\0"; defExt = "ogg"; }

                        char filePath[MAX_PATH] = {};
                        OPENFILENAMEA ofn = {};
                        ofn.lStructSize  = sizeof(ofn);
                        ofn.hwndOwner    = hwnd_;
                        ofn.lpstrFilter  = filter;
                        ofn.lpstrFile    = filePath;
                        ofn.nMaxFile     = MAX_PATH;
                        ofn.lpstrDefExt  = defExt;
                        ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

                        if (!GetSaveFileNameA(&ofn)) {
                            // User cancelled — revert button state.
                            controlBar_.setRecordState(false);
                            return 0;
                        }

                        int fd = _open(filePath, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0644);
                        if (fd < 0) {
                            controlBar_.setRecordState(false);
                            MessageBoxA(hwnd_, "Failed to open output file.",
                                        "Opiqo — Record Error", MB_OK | MB_ICONERROR);
                            return 0;
                        }

                        int quality = controlBar_.qualityIndex();
                        bool ok = liveEngine_.startRecording(fd, static_cast<int>(formatMap[fmtIdx]), quality);
                        if (!ok) {
                            _close(fd);
                            controlBar_.setRecordState(false);
                            MessageBoxA(hwnd_, "Failed to start recording.",
                                        "Opiqo — Record Error", MB_OK | MB_ICONERROR);
                            return 0;
                        }

                        recordingFd_ = fd;
                        settings_.recordFormat  = fmtIdx;
                        settings_.recordQuality = quality;
                    } else {
                        liveEngine_.stopRecording();
                        if (recordingFd_ >= 0) {
                            _close(recordingFd_);
                            recordingFd_ = -1;
                        }
                        controlBar_.setRecordState(false);
                    }
                    return 0;
                }
                case IDC_FORMAT_COMBO: {
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int fmtIdx = controlBar_.formatIndex();
                        settings_.recordFormat = fmtIdx;
                        controlBar_.showQualityCombo(fmtIdx != 0);
                    }
                    return 0;
                }
                default: {
                    // Slot Add buttons: IDC_SLOT_ADD_BASE + 0..3
                    int id = static_cast<int>(LOWORD(wParam));
                    if (id >= IDC_SLOT_ADD_BASE && id < IDC_SLOT_ADD_BASE + 4) {
                        int i = id - IDC_SLOT_ADD_BASE;
                        std::string uri;
                        if (PluginDialog::showModal(hwnd_, &liveEngine_, &uri)) {
                            liveEngine_.addPlugin(i + 1, uri);
                            std::string name;
                            json all = liveEngine_.getAvailablePlugins();
                            if (all.contains(uri) &&
                                all[uri].contains("name") &&
                                all[uri]["name"].is_string())
                                name = all[uri]["name"].get<std::string>();
                            else
                                name = uri;
                            slots_[i].setPlugin(name.c_str());
                            slots_[i].buildParameterPanel(&liveEngine_);
                            slotEnabled_[i] = true;
                        }
                        return 0;
                    }
                    // Slot Bypass buttons: IDC_SLOT_BYPASS_BASE + 0..3
                    if (id >= IDC_SLOT_BYPASS_BASE && id < IDC_SLOT_BYPASS_BASE + 4) {
                        int i = id - IDC_SLOT_BYPASS_BASE;
                        slotEnabled_[i] = !slotEnabled_[i];
                        liveEngine_.setPluginEnabled(i + 1, slotEnabled_[i]);
                        slots_[i].setBypassVisual(!slotEnabled_[i]);
                        return 0;
                    }
                    // Slot Delete buttons: IDC_SLOT_DELETE_BASE + 0..3
                    if (id >= IDC_SLOT_DELETE_BASE && id < IDC_SLOT_DELETE_BASE + 4) {
                        int i = id - IDC_SLOT_DELETE_BASE;
                        liveEngine_.deletePlugin(i + 1);
                        slots_[i].clearParameterPanel();
                        slots_[i].clearPlugin();
                        slotEnabled_[i] = true;
                        return 0;
                    }
                    break;
                }
            }
            break;
        }

        case WM_TIMER:
            if (wParam == IDT_ENGINE_STATE) {
                onEngineStatePoll();
                return 0;
            }
            if (wParam == IDT_ENGINE_WATCHDOG) {
                if (audioEngine_.state() == AudioEngine::State::Error) {
                    KillTimer(hwnd_, IDT_ENGINE_WATCHDOG);
                    onEngineError();
                } else if (statusBar_) {
                    // Refresh dropout count in the output pane.
                    uint64_t d = audioEngine_.dropoutCount();
                    std::string outText = "Out: ";
                    auto outputs = deviceEnum_->enumerateOutputDevices();
                    for (const auto& dev : outputs)
                        if (dev.id == settings_.outputDeviceId) {
                            outText += dev.friendlyName;
                            break;
                        }
                    if (d > 0)
                        outText += "  [" + std::to_string(d)
                                   + (d == 1 ? " dropout" : " dropouts") + "]";
                    SendMessageA(statusBar_, SB_SETTEXTA, 1,
                                 reinterpret_cast<LPARAM>(outText.c_str()));
                }
                return 0;
            }
            break;
        case WM_OPIQO_DEVICE_CHANGE:
            onDeviceListChanged();
            return 0;

        case WM_SIZE:
            doLayout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = kMinWidth;
            mmi->ptMinTrackSize.y = kMinHeight;
            return 0;
        }

        case WM_CLOSE:
            if (recordingFd_ >= 0) {
                int choice = MessageBoxA(hwnd_,
                    "A recording is in progress. Stop recording and close?",
                    "Opiqo \u2014 Recording Active", MB_YESNO | MB_ICONWARNING);
                if (choice != IDYES)
                    return 0;  // User cancelled — leave app running.
                liveEngine_.stopRecording();
                _close(recordingFd_);
                recordingFd_ = -1;
                controlBar_.setRecordState(false);
            }
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            // Safety net: finalise any open recording (normally handled in WM_CLOSE).
            if (recordingFd_ >= 0) {
                liveEngine_.stopRecording();
                _close(recordingFd_);
                recordingFd_ = -1;
            }
            settings_.save();
            audioEngine_.stop();
            deviceEnum_.reset();   // Release COM objects before CoUninitialize.
            CoUninitialize();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProc(hwnd_, message, wParam, lParam);
}

