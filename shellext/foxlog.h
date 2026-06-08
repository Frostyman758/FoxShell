#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

// Dirt-simple append logger to %TEMP%\foxshellext.log for diagnosing how far the
// shell gets when hosting us. Compiled in only when FOX_LOG is defined.
#ifdef FOX_LOG
inline void FoxLog(const char* fmt, ...)
{
    // Fixed shared path so the user's real explorer AND the sandboxed dev shell
    // both read the same file (C:\rsearch is real/non-redirected).
    const char* path = "C:\\rsearch\\foxshellext.log";
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char msg[1024];
    int p = sprintf_s(msg, "[pid %lu] ", GetCurrentProcessId());
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(msg + p, sizeof(msg) - p - 2, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    int len = p + n;
    msg[len++] = '\r'; msg[len++] = '\n';
    DWORD w; WriteFile(h, msg, len, &w, NULL);
    CloseHandle(h);
}
inline void FoxLogIID(const char* tag, REFIID riid)
{
    wchar_t g[64]; StringFromGUID2(riid, g, 64);
    char a[80]; WideCharToMultiByte(CP_ACP, 0, g, -1, a, 80, NULL, NULL);
    FoxLog("%s %s", tag, a);
}
#else
inline void FoxLog(const char*, ...) {}
inline void FoxLogIID(const char*, REFIID) {}
#endif
