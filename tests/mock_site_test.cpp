// Verify our context-menu wrapper, when given a site and asked to invoke "open",
// actually reaches out for IShellBrowser to navigate. A mock IServiceProvider
// logs what service the wrapper requests. Confirms the navigation path without
// needing real Explorer.
// Usage: mock_site_test.exe <foxshellext.dll> <archive> <subFolder>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <servprov.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

typedef HRESULT (__stdcall *pfnGCO)(REFCLSID, REFIID, void**);
static const CLSID CLSID_Fox =
{ 0x5aa92d71, 0x4013, 0x463e, { 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf } };

struct MockSite : IServiceProvider {
    LONG ref = 1;
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IServiceProvider) { *ppv = static_cast<IServiceProvider*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return ++ref; }
    IFACEMETHODIMP_(ULONG) Release() { return --ref; }
    IFACEMETHODIMP QueryService(REFGUID svc, REFIID riid, void** ppv) {
        wchar_t s[64], i[64]; StringFromGUID2(svc, s, 64); StringFromGUID2(riid, i, 64);
        wprintf(L"  [mock] QueryService svc=%s riid=%s\n", s, i);
        *ppv = nullptr; return E_NOINTERFACE; // we only care THAT it asked for IShellBrowser
    }
};

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { wprintf(L"usage: mock_site_test <dll> <archive> <sub>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE m = LoadLibraryW(argv[1]);
    auto gco = (pfnGCO)GetProcAddress(m, "DllGetClassObject");
    IClassFactory* cf = nullptr; gco(CLSID_Fox, IID_IClassFactory, (void**)&cf);
    IPersistFolder* pf = nullptr; cf->CreateInstance(nullptr, IID_IPersistFolder, (void**)&pf); cf->Release();
    PIDLIST_ABSOLUTE pidl = nullptr; SHParseDisplayName(argv[2], nullptr, &pidl, 0, nullptr);
    pf->Initialize(pidl);
    IShellFolder* sf = nullptr; pf->QueryInterface(IID_IShellFolder, (void**)&sf);

    IEnumIDList* e = nullptr; sf->EnumObjects(nullptr, SHCONTF_FOLDERS|SHCONTF_NONFOLDERS, &e);
    PITEMID_CHILD c = nullptr, found = nullptr; ULONG f = 0;
    while (e->Next(1,&c,&f)==S_OK && f==1) {
        STRRET sr; wchar_t nm[260]; sf->GetDisplayNameOf(c, SHGDN_NORMAL, &sr); StrRetToBufW(&sr,c,nm,260);
        if (_wcsicmp(nm, argv[3])==0) { found=(PITEMID_CHILD)ILClone(c); CoTaskMemFree(c); break; }
        CoTaskMemFree(c);
    }
    e->Release();
    if (!found) { wprintf(L"sub not found\n"); return 1; }

    IContextMenu* cm = nullptr;
    sf->GetUIObjectOf(nullptr, 1, (PCUITEMID_CHILD_ARRAY)&found, IID_IContextMenu, nullptr, (void**)&cm);

    IObjectWithSite* ows = nullptr;
    HRESULT hr = cm->QueryInterface(IID_PPV_ARGS(&ows));
    wprintf(L"wrapper supports IObjectWithSite: %s\n", SUCCEEDED(hr) ? L"YES" : L"NO");
    if (SUCCEEDED(hr)) {
        MockSite site;
        ows->SetSite(static_cast<IServiceProvider*>(&site));
        wprintf(L"invoking 'open' (watch for IShellBrowser request):\n");
        CMINVOKECOMMANDINFO ici = { sizeof(ici) };
        ici.lpVerb = "open";
        HRESULT ir = cm->InvokeCommand(&ici);
        wprintf(L"InvokeCommand('open') hr=0x%08X\n", ir);
        ows->SetSite(nullptr);
        ows->Release();
    }
    cm->Release();
    return 0;
}
