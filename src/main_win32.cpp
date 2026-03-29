#include <windows.h>

#include "win32/MainWindow.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    MainWindow app(hInstance);
    if (!app.create(nCmdShow)) {
        MessageBoxA(nullptr, "Failed to create main window", "Opiqo", MB_ICONERROR | MB_OK);
        return 1;
    }

    return app.run();
}
