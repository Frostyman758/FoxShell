// Verify an inner folder item now yields a default context menu whose default
// verb is "open" (== what Explorer invokes on double-click). No Explorer needed.
// Usage: ctx_test.exe <foxshellext.dll> <archive> <leafFolderName>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

typedef HRESULT (__stdcall *pfnGCO)(REFCLSID, REFIID, void**);
static const CLSID CLSID_Fox =
{ 0x5aa92d71, 0x4013, 0x463e, { 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf } };

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) { wprintf(L"usage: ctx_test <dll> <archive> <leafFolder>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE m = LoadLibraryW(argv[1]);
    auto gco = (pfnGCO)GetProcAddress(m, "DllGetClassObject");
    IClassFactory* cf = nullptr; gco(CLSID_Fox, IID_IClassFactory, (void**)&cf);
    IPersistFolder* pf = nullptr; cf->CreateInstance(nullptr, IID_IPersistFolder, (void**)&pf); cf->Release();
    PIDLIST_ABSOLUTE pidl = nullptr; SHParseDisplayName(argv[2], nullptr, &pidl, 0, nullptr);
    pf->Initialize(pidl);
    IShellFolder* sf = nullptr; pf->QueryInterface(IID_IShellFolder, (void**)&sf);

    IEnumIDList* e = nullptr; sf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD child = nullptr; ULONG f = 0; PITEMID_CHILD found = nullptr;
    while (e->Next(1, &child, &f) == S_OK && f == 1) {
        STRRET sr; wchar_t nm[260];
        sf->GetDisplayNameOf(child, SHGDN_NORMAL, &sr); StrRetToBufW(&sr, child, nm, 260);
        if (_wcsicmp(nm, argv[3]) == 0) { found = (PITEMID_CHILD)ILClone(child); CoTaskMemFree(child); break; }
        CoTaskMemFree(child);
    }
    e->Release();
    if (!found) { wprintf(L"leaf '%s' not found\n", argv[3]); return 1; }

    IContextMenu* cm = nullptr;
    HRESULT hr = sf->GetUIObjectOf(nullptr, 1, (PCUITEMID_CHILD_ARRAY)&found, IID_IContextMenu, nullptr, (void**)&cm);
    wprintf(L"GetUIObjectOf(IContextMenu) hr=0x%08X  %s\n", hr, SUCCEEDED(hr) ? L"<- menu provided" : L"<- NO menu (double-click would do nothing)");
    if (SUCCEEDED(hr) && cm) {
        HMENU menu = CreatePopupMenu();
        HRESULT qc = cm->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
        int n = GetMenuItemCount(menu);
        int def = GetMenuDefaultItem(menu, FALSE, 0);
        wprintf(L"  QueryContextMenu hr=0x%08X items=%d defaultItemId=%d\n", qc, n, def);
        if (def >= 0) {
            char verb[64] = "";
            HRESULT gv = cm->GetCommandString(def - 1, GCS_VERBA, nullptr, verb, 64);
            wprintf(L"  default verb = '%hs' (gv=0x%08X)\n", verb, gv);
            wprintf(L"  %s\n", (gv == S_OK && _stricmp(verb, "open") == 0) ? L"RESULT: default action is OPEN -> double-click will navigate." :
                                L"RESULT: default verb present (navigation likely).");
        }
        DestroyMenu(menu); cm->Release();
    }
    return 0;
}
