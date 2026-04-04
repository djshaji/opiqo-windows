#include <windows.h>
#include <cstdio>
#include <dbghelp.h>

#include "win32/MainWindow.h"
#include "win32/win_logging.h"

// ---------------------------------------------------------------------------
// Crash handler — writes opiqo_crash.dmp next to the executable.
// ---------------------------------------------------------------------------
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    // Replace the filename portion with "opiqo_crash.dmp".
    char* sep = strrchr(exePath, '\\');
    if (sep) *(sep + 1) = '\0';
    strcat_s(exePath, "opiqo_crash.dmp");

    HANDLE hFile = CreateFileA(exePath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile,
                          (MINIDUMP_TYPE)(MiniDumpWithDataSegs |
                                         MiniDumpWithFullMemoryInfo |
                                         MiniDumpWithThreadInfo),
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;   // let Windows show the crash dialog too
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Enable per-monitor DPI awareness before any window is created.
    // Loaded dynamically so the binary still runs on Windows 8.1 (where the
    // function does not exist); on those systems we fall back to unaware mode.
    using FnSetDpiCtx = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (auto fn = reinterpret_cast<FnSetDpiCtx>(
            GetProcAddress(GetModuleHandleA("user32.dll"),
                           "SetProcessDpiAwarenessContext")))
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    SetUnhandledExceptionFilter(crashHandler);

    // Try to load DrMingw for human-readable stack traces (optional, no-op if absent).
    LoadLibraryA("exchndl.dll");
    // If launched from a console (cmd.exe / PowerShell), attach to it so that
    // stdout and stderr are visible there.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
    }

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
