// Replicate a double-click's default-verb invocation on the file, headlessly.
// ShellExecuteEx with the absolute pidl and a null verb runs the same default
// action Explorer runs on double-click. If our junction is wired right this
// navigates / loads our DLL; the log tells us how far it gets.
//
// Usage: invoke_test.exe <archivePath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { wprintf(L"usage: invoke_test <archive>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(argv[1], nullptr, &pidl, 0, nullptr);
    wprintf(L"parse hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpIDList = pidl;
    sei.nShow = SW_SHOWNORMAL;
    sei.lpVerb = nullptr; // default verb == what double-click invokes

    BOOL ok = ShellExecuteExW(&sei);
    unsigned le = (unsigned)GetLastError();
    wprintf(L"ShellExecuteEx ok=%d err=%u\n", ok, le);

    Sleep(2000); // give any spawned navigation time to bind our handler
    CoTaskMemFree(pidl);
    CoUninitialize();
    return 0;
}
