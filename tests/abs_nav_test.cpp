// Replicate what "navigate into Assets" does: build Assets' ABSOLUTE pidl
// ([.dat junction][Assets]) and bind it through the real shell (SHBindToObject),
// exactly as a double-click navigation would. If this works, the bind path is
// fine and the problem is DefView's double-click routing; if it fails, the
// junction can't be re-entered at a deeper interior path.
// Usage: abs_nav_test.exe <archive> <leafFolder>
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
    if (argc < 3) { wprintf(L"usage: abs_nav_test <archive> <leafFolder>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    PIDLIST_ABSOLUTE pidlDat = nullptr;
    HRESULT hr = SHParseDisplayName(argv[1], nullptr, &pidlDat, 0, nullptr);
    wprintf(L"parse .dat hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    IShellFolder* root = nullptr;
    hr = SHBindToObject(nullptr, pidlDat, nullptr, IID_IShellFolder, (void**)&root);
    wprintf(L"bind junction root hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    // find the leaf child pidl
    IEnumIDList* e = nullptr; root->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD child = nullptr, found = nullptr; ULONG f = 0;
    while (e->Next(1, &child, &f) == S_OK && f == 1) {
        STRRET sr; wchar_t nm[260]; root->GetDisplayNameOf(child, SHGDN_NORMAL, &sr); StrRetToBufW(&sr, child, nm, 260);
        if (_wcsicmp(nm, argv[2]) == 0) { found = (PITEMID_CHILD)ILClone(child); CoTaskMemFree(child); break; }
        CoTaskMemFree(child);
    }
    e->Release();
    if (!found) { wprintf(L"leaf '%s' not found\n", argv[2]); return 1; }

    // absolute pidl = [.dat junction] + [leaf], then bind it through the shell
    PIDLIST_ABSOLUTE pidlLeaf = ILCombine(pidlDat, found);
    IShellFolder* sub = nullptr;
    hr = SHBindToObject(nullptr, pidlLeaf, nullptr, IID_IShellFolder, (void**)&sub);
    wprintf(L"SHBindToObject(absolute '%s') hr=0x%08X  %s\n", argv[2], hr,
            SUCCEEDED(hr) ? L"<- deep re-bind WORKS" : L"<- deep re-bind FAILS (this is the navigation bug)");
    if (SUCCEEDED(hr) && sub) {
        IEnumIDList* e2 = nullptr;
        if (SUCCEEDED(sub->EnumObjects(nullptr, SHCONTF_FOLDERS|SHCONTF_NONFOLDERS, &e2)) && e2) {
            int n = 0; PITEMID_CHILD c = nullptr; ULONG ff = 0;
            while (e2->Next(1,&c,&ff)==S_OK && ff==1) { n++; CoTaskMemFree(c); }
            wprintf(L"  '%s' contains %d items\n", argv[2], n);
            e2->Release();
        }
        sub->Release();
    }
    return 0;
}
