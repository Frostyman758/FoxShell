// Verify GetDetailsEx(PKEY_ItemTypeText) returns the friendly per-item type.
// Usage: type_test.exe <archive>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "oleaut32.lib")

static const PROPERTYKEY PKEY_ItemTypeText_ =
{ { 0x28636AA6, 0x953D, 0x11D2, { 0xB5, 0xD6, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } }, 4 };

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) { wprintf(L"usage: type_test <archive>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE p = nullptr;
    if (FAILED(SHParseDisplayName(argv[1], nullptr, &p, 0, nullptr))) { wprintf(L"parse fail\n"); return 1; }
    IShellFolder2* sf = nullptr;
    if (FAILED(SHBindToObject(nullptr, p, nullptr, IID_IShellFolder2, (void**)&sf))) { wprintf(L"bind IShellFolder2 fail\n"); return 1; }

    IEnumIDList* e = nullptr; sf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD c = nullptr; ULONG f = 0;
    wprintf(L"=== %s ===\n  %-32s %s\n", argv[1], L"NAME", L"TYPE");
    while (e->Next(1, &c, &f) == S_OK && f == 1)
    {
        STRRET sr; wchar_t nm[260] = L"?"; sf->GetDisplayNameOf(c, SHGDN_NORMAL, &sr); StrRetToBufW(&sr, c, nm, 260);
        VARIANT v; VariantInit(&v);
        HRESULT hr = sf->GetDetailsEx(c, &PKEY_ItemTypeText_, &v);
        wprintf(L"  %-32s %s\n", nm, (SUCCEEDED(hr) && v.vt == VT_BSTR) ? v.bstrVal : L"<none>");
        VariantClear(&v);
        CoTaskMemFree(c);
    }
    e->Release(); sf->Release();
    CoUninitialize();
    return 0;
}
