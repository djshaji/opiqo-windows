#include "MainWindow.h"

#include "resource.h"

// Posted from the COM notification thread; handled on the UI thread.
static constexpr UINT WM_OPIQO_DEVICE_CHANGE = WM_APP + 1;

MainWindow::MainWindow(HINSTANCE instance)
    : instance_(instance) {}

MainWindow::~MainWindow() = default;

bool MainWindow::create(int nCmdShow) {
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

    hwnd_ = CreateWindowExA(
        0,
        className,
        "Opiqo Windows Host",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
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

void MainWindow::onDeviceListChanged() {
    auto inputs  = deviceEnum_->enumerateInputDevices();
    auto outputs = deviceEnum_->enumerateOutputDevices();

    settings_.inputDeviceId  = WasapiDeviceEnum::resolveOrDefault(
        inputs,  settings_.inputDeviceId);
    settings_.outputDeviceId = WasapiDeviceEnum::resolveOrDefault(
        outputs, settings_.outputDeviceId);
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
                case IDM_FILE_EXPORT_PRESET:
                case IDM_FILE_IMPORT_PRESET:
                case IDM_SETTINGS_OPEN:
                    MessageBoxA(hwnd_, "This action is scaffolded in Milestone 0.",
                                "Opiqo", MB_OK);
                    return 0;
                default:
                    break;
            }
            break;
        }
        case WM_OPIQO_DEVICE_CHANGE:
            onDeviceListChanged();
            return 0;

        case WM_DESTROY:
            settings_.save();
            deviceEnum_.reset();   // Release COM objects before CoUninitialize.
            CoUninitialize();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProc(hwnd_, message, wParam, lParam);
}

