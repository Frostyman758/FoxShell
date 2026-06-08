// Host IExplorerBrowser (what foxbrowse.exe uses) in a hidden window, navigate
// to the .dat root, then BrowseToIDList into a sub-folder. If our NSE gets a
// view + EnumObjects for the sub-folder, navigation works in this host even
// though Win11's main Explorer view won't trigger it on double-click.
// Usage: ieb_nav_test.exe <archive> <leafFolder>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

static void pump(int ms) {
    DWORD end = GetTickCount() + ms; MSG m;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(10);
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: ieb_nav_test <archive> <leafFolder>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"IebTest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"IebTest", L"ieb", WS_OVERLAPPEDWINDOW, 0, 0, 900, 600, nullptr, nullptr, wc.hInstance, nullptr);

    IExplorerBrowser* eb = nullptr;
    CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&eb));
    RECT rc; GetClientRect(hwnd, &rc);
    FOLDERSETTINGS fs = { FVM_DETAILS, FWF_NONE };
    HRESULT hr = eb->Initialize(hwnd, &rc, &fs);
    wprintf(L"IEB Initialize hr=0x%08X\n", hr);

    PIDLIST_ABSOLUTE pidlDat = nullptr; SHParseDisplayName(argv[1], nullptr, &pidlDat, 0, nullptr);
    hr = eb->BrowseToIDList(pidlDat, SBSP_ABSOLUTE);
    wprintf(L"BrowseTo(.dat) hr=0x%08X\n", hr);
    pump(800);

    // find the leaf child via a fresh bind of the root
    IShellFolder* root = nullptr; SHBindToObject(nullptr, pidlDat, nullptr, IID_IShellFolder, (void**)&root);
    IEnumIDList* e = nullptr; root->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD child = nullptr, found = nullptr; ULONG f = 0;
    while (e->Next(1, &child, &f) == S_OK && f == 1) {
        STRRET sr; wchar_t nm[260]; root->GetDisplayNameOf(child, SHGDN_NORMAL, &sr); StrRetToBufW(&sr, child, nm, 260);
        if (_wcsicmp(nm, argv[2]) == 0) { found = (PITEMID_CHILD)ILClone(child); CoTaskMemFree(child); break; }
        CoTaskMemFree(child);
    }
    e->Release(); root->Release();
    if (!found) { wprintf(L"leaf not found\n"); return 1; }

    PIDLIST_ABSOLUTE pidlLeaf = ILCombine(pidlDat, found);
    hr = eb->BrowseToIDList(pidlLeaf, SBSP_ABSOLUTE);
    wprintf(L"BrowseTo('%s') hr=0x%08X  %s\n", argv[2], hr,
            SUCCEEDED(hr) ? L"<- IEB navigated into the sub-folder" : L"<- IEB navigation FAILED");
    pump(800);

    eb->Destroy(); eb->Release();
    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
