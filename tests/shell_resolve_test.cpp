// Verify the REGISTRY wiring end-to-end through the real shell: parse the .dat
// path, then ask the shell to bind it as a folder. If our junction is wired
// correctly, the shell instantiates our registered CLSID and we enumerate the
// archive — i.e. exactly what Explorer will do, but headless.
//
// Usage: shell_resolve_test.exe <archivePath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { wprintf(L"usage: shell_resolve_test <archive>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF parsed = 0;
    HRESULT hr = SHParseDisplayName(argv[1], nullptr, &pidl,
                                    SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_STREAM, &parsed);
    wprintf(L"SHParseDisplayName hr=0x%08X  attrs=0x%08X  FOLDER=%d BROWSABLE=%d STREAM=%d\n",
            hr, parsed, !!(parsed & SFGAO_FOLDER), !!(parsed & SFGAO_BROWSABLE), !!(parsed & SFGAO_STREAM));
    if (FAILED(hr)) return 1;

    IShellFolder* sf = nullptr;
    hr = SHBindToObject(nullptr, pidl, nullptr, IID_IShellFolder, (void**)&sf);
    wprintf(L"SHBindToObject(IShellFolder) hr=0x%08X  %s\n", hr,
            SUCCEEDED(hr) ? L"<- shell mounted our junction" : L"<- NOT mounted");
    if (FAILED(hr) || !sf) { CoTaskMemFree(pidl); return 1; }

    IEnumIDList* e = nullptr;
    hr = sf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    wprintf(L"EnumObjects hr=0x%08X\n", hr);
    int n = 0;
    if (SUCCEEDED(hr) && e)
    {
        PITEMID_CHILD c = nullptr; ULONG f = 0;
        while (e->Next(1, &c, &f) == S_OK && f == 1)
        {
            STRRET sr; wchar_t nm[260] = L"?";
            if (SUCCEEDED(sf->GetDisplayNameOf(c, SHGDN_NORMAL, &sr))) StrRetToBufW(&sr, c, nm, 260);
            SFGAOF a = SFGAO_FOLDER; sf->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&c, &a);
            wprintf(L"  %s %s\n", (a & SFGAO_FOLDER) ? L"[D]" : L"   ", nm);
            CoTaskMemFree(c);
            if (++n >= 20) { wprintf(L"  ...\n"); break; }
        }
        e->Release();
    }
    // The piece Explorer needs to actually DISPLAY the folder.
    IUnknown* view = nullptr;
    HRESULT vhr = sf->CreateViewObject(nullptr, IID_IShellView, (void**)&view);
    wprintf(L"CreateViewObject(IShellView) hr=0x%08X\n", vhr);
    if (view) view->Release();

    wprintf(L"\n%s\n", n > 0 ? L"RESULT: shell-mounted browse WORKS." : L"RESULT: no entries.");
    sf->Release();
    CoTaskMemFree(pidl);
    CoUninitialize();
    return 0;
}
