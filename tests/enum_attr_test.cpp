// Replicate EXACTLY what Explorer's folder view does:
//   desktop -> bind parent folder (Z:\) -> EnumObjects -> find our file ->
//   GetAttributesOf (does CFSFolder think it's a folder?) -> BindToObject into it.
// If BindToObject loads our DLL and returns an IShellFolder, the junction works
// at the view level and the problem is purely Explorer's double-click routing.
//
// Usage: enum_attr_test.exe <parentDir> <leafName>
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
    if (argc < 3) { wprintf(L"usage: enum_attr_test <parentDir> <leafName>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellFolder* desktop = nullptr;
    SHGetDesktopFolder(&desktop);

    PIDLIST_ABSOLUTE parentPidl = nullptr;
    HRESULT hr = SHParseDisplayName(argv[1], nullptr, &parentPidl, 0, nullptr);
    wprintf(L"parse parent '%s' hr=0x%08X\n", argv[1], hr);

    IShellFolder* parent = nullptr;
    hr = SHBindToObject(nullptr, parentPidl, nullptr, IID_IShellFolder, (void**)&parent);
    wprintf(L"bind parent hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    IEnumIDList* e = nullptr;
    parent->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD child = nullptr; ULONG f = 0; bool found = false;
    while (e && e->Next(1, &child, &f) == S_OK && f == 1)
    {
        STRRET sr; wchar_t nm[260] = L"";
        if (SUCCEEDED(parent->GetDisplayNameOf(child, SHGDN_INFOLDER | SHGDN_FORPARSING, &sr)))
            StrRetToBufW(&sr, child, nm, 260);
        if (_wcsicmp(nm, argv[2]) == 0)
        {
            found = true;
            SFGAOF attr = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_STREAM | SFGAO_BROWSABLE | SFGAO_LINK;
            parent->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attr);
            wprintf(L"FOUND '%s'\n  attrs=0x%08X  FOLDER=%d HASSUBFOLDER=%d STREAM=%d BROWSABLE=%d\n",
                    nm, attr, !!(attr&SFGAO_FOLDER), !!(attr&SFGAO_HASSUBFOLDER),
                    !!(attr&SFGAO_STREAM), !!(attr&SFGAO_BROWSABLE));

            IShellFolder* sub = nullptr;
            HRESULT bhr = parent->BindToObject(child, nullptr, IID_IShellFolder, (void**)&sub);
            wprintf(L"  parent->BindToObject(IShellFolder) hr=0x%08X  %s\n", bhr,
                    SUCCEEDED(bhr) ? L"<- junction mounts via CFSFolder" : L"<- CFSFolder did NOT delegate to us");
            if (sub)
            {
                IEnumIDList* e2 = nullptr;
                if (SUCCEEDED(sub->EnumObjects(nullptr, SHCONTF_FOLDERS|SHCONTF_NONFOLDERS, &e2)) && e2)
                {
                    int n=0; PITEMID_CHILD c2=nullptr; ULONG f2=0;
                    while (e2->Next(1,&c2,&f2)==S_OK && f2==1) { n++; CoTaskMemFree(c2); }
                    wprintf(L"  inner entries: %d\n", n);
                    e2->Release();
                }
                sub->Release();
            }
            CoTaskMemFree(child);
            break;
        }
        CoTaskMemFree(child);
    }
    if (e) e->Release();
    if (!found) wprintf(L"leaf '%s' not found in enumeration\n", argv[2]);

    parent->Release();
    desktop->Release();
    CoUninitialize();
    return 0;
}
