#include <windows.h>

#include "win32/MainWindow.h"
#include "win32/win_logging.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    opiqo_log_init();

    MainWindow app(hInstance);
    if (!app.create(nCmdShow)) {
        MessageBoxA(nullptr, "Failed to create main window", "Opiqo", MB_ICONERROR | MB_OK);
        opiqo_log_shutdown();
        return 1;
    }

    int ret = app.run();
    opiqo_log_shutdown();
    return ret;
}
