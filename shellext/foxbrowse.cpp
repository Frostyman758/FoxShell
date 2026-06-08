// foxbrowse.exe — opens an MGSV archive (.dat/.fpk/...) in an Explorer-style
// window, navigated INTO the archive via our namespace extension. This is the
// reliable double-click entry point: a normal exe launch always fires, and
// IExplorerBrowser binds the archive pidl through the same NSE path that works
// programmatically (CFSFolder delegates to our IShellFolder). Once inside,
// subfolders and nested archives navigate normally.
//
// Usage: foxbrowse.exe <archivePath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include "../include/foxmagic.h"
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

static IExplorerBrowser* g_peb = nullptr;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
        if (g_peb) { RECT rc; GetClientRect(h, &rc); g_peb->SetRect(nullptr, rc); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) { MessageBoxW(nullptr, L"Usage: foxbrowse <archive path>", L"Fox Browse", MB_OK | MB_ICONINFORMATION); return 2; }
    std::wstring path = argv[1];

    // Content gate: only browse files whose bytes are a real Fox archive. A
    // generic .dat (save game, video, some other app's data) is NOT ours — fall
    // back to the standard "Open with" picker instead of a failed-archive error,
    // so foxbrowse behaves as if it weren't involved for non-Fox files.
    if (!FoxIsArchiveFile(path.c_str()))
    {
        ShellExecuteW(nullptr, L"openas", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FoxBrowseWnd";
    RegisterClassW(&wc);

    std::wstring title = std::wstring(L"Fox Archive  —  ") + PathFindFileNameW(path.c_str());
    HWND hwnd = CreateWindowW(L"FoxBrowseWnd", title.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 700,
                              nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_peb));
    if (FAILED(hr) || !g_peb)
    { MessageBoxW(hwnd, L"Failed to create the Explorer browser control.", L"Fox Browse", MB_OK | MB_ICONERROR); return 1; }

    RECT rc; GetClientRect(hwnd, &rc);
    FOLDERSETTINGS fs = { FVM_DETAILS, FWF_NONE };
    hr = g_peb->Initialize(hwnd, &rc, &fs);
    if (FAILED(hr))
    { MessageBoxW(hwnd, L"Failed to initialize the Explorer browser control.", L"Fox Browse", MB_OK | MB_ICONERROR); return 1; }

    PIDLIST_ABSOLUTE pidl = nullptr;
    hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
    if (SUCCEEDED(hr))
    {
        hr = g_peb->BrowseToIDList(pidl, SBSP_ABSOLUTE);
        CoTaskMemFree(pidl);
    }
    if (FAILED(hr))
    {
        std::wstring msg = L"Could not open archive:\n" + path +
                           L"\n\n(Is the shell extension installed? hr=0x" ;
        wchar_t hx[16]; wsprintfW(hx, L"%08X", hr); msg += hx; msg += L")";
        MessageBoxW(hwnd, msg.c_str(), L"Fox Browse", MB_OK | MB_ICONWARNING);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    if (g_peb) { g_peb->Destroy(); g_peb->Release(); }
    CoUninitialize();
    return 0;
}
