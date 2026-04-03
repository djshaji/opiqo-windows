#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "win_logging.h"

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static FILE*       s_logFile    = nullptr;
static std::mutex  s_logMutex;

// Rate-limit table: key = FNV-1a hash of (file, line), value = last tick.
static std::unordered_map<uint64_t, ULONGLONG> s_rateLimitTable;
static constexpr ULONGLONG kRateLimitMs = 1000ULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// FNV-1a 64-bit hash of a C-string + integer — cheap, header-free.
static uint64_t fnv1a(const char* str, int line) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*str++));
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<uint64_t>(line);
    hash *= 1099511628211ULL;
    return hash;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void opiqo_log_init(void) {
    std::lock_guard<std::mutex> lk(s_logMutex);
    if (s_logFile) return;  // Already initialised.

    // Resolve %APPDATA%\Opiqo\ without pulling in shlobj.h.
    char appData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;  // Can't determine path — log to debugger only.

    char dir[MAX_PATH];
    _snprintf(dir, sizeof(dir) - 1, "%s\\Opiqo", appData);
    dir[sizeof(dir) - 1] = '\0';
    CreateDirectoryA(dir, nullptr);  // No-op if already exists.

    char path[MAX_PATH];
    _snprintf(path, sizeof(path) - 1, "%s\\opiqo.log", dir);
    path[sizeof(path) - 1] = '\0';

    s_logFile = fopen(path, "a");
    // Failure is non-fatal — output will still go to OutputDebugStringA.
}

void opiqo_log_shutdown(void) {
    std::lock_guard<std::mutex> lk(s_logMutex);
    if (s_logFile) {
        fflush(s_logFile);
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void opiqo_win_log(const char* file, int line, const char* level,
                   const char* fmt, ...) {
    // Rate-limit: suppress repeated messages from the same call site.
    uint64_t key = fnv1a(file, line);
    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lk(s_logMutex);
        auto it = s_rateLimitTable.find(key);
        if (it != s_rateLimitTable.end()) {
            if (now - it->second < kRateLimitMs)
                return;
            it->second = now;
        } else {
            s_rateLimitTable.emplace(key, now);
        }
    }

    // Format the caller's message.
    char body[480];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(body, sizeof(body) - 1, fmt, ap);
    va_end(ap);
    body[sizeof(body) - 1] = '\0';

    // Compose full line: "[LEVEL file:line] body\n"
    // Extract basename only to keep lines short.
    const char* base = file;
    for (const char* p = file; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;

    char line_buf[512];
    _snprintf(line_buf, sizeof(line_buf) - 1,
              "[%s %s:%d] %s\n", level, base, line, body);
    line_buf[sizeof(line_buf) - 1] = '\0';

    OutputDebugStringA(line_buf);
    fputs(line_buf, stderr);

    std::lock_guard<std::mutex> lk(s_logMutex);
    if (s_logFile) {
        fputs(line_buf, s_logFile);
        fflush(s_logFile);
    }
}
