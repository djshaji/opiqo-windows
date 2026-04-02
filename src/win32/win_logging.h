#pragma once

// Windows-specific logging backend.
// Call opiqo_log_init() once at startup and opiqo_log_shutdown() at exit.
// All LOG* macros in logging_macros.h route here when _WIN32 is defined.

#ifdef __cplusplus
extern "C" {
#endif

// Open %APPDATA%\Opiqo\opiqo.log for append. Creates the directory if absent.
// Safe to call multiple times; only the first call has any effect.
void opiqo_log_init(void);

// Flush and close the log file. After this call log output goes only to
// OutputDebugStringA.
void opiqo_log_shutdown(void);

// Internal sink called by the LOG* macros. Not for direct use.
// Rate-limited per (file, line): repeated calls from the same site within
// 1 000 ms are suppressed after the first occurrence.
void opiqo_win_log(const char* file, int line, const char* level,
                   const char* fmt, ...);

#ifdef __cplusplus
}
#endif
