// Validate that FoxShellFolder serves a thumbnail for a virtual .ftex item via
// GetUIObjectOf(IThumbnailProvider) — exactly the call Explorer makes for NSE
// items. Drives our folder DIRECTLY (DllGetClassObject + IPersistFolder3), so
// no shell registration is needed. Navigates archive -> dir, finds a .ftex
// child, asks the folder for its thumbnail, saves PNG.
// Usage: nse_thumb_test.exe <foxshellext.dll> <archive> <dir/slashes> <outPng>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")

typedef HRESULT (__stdcall *pfnGCO)(REFCLSID, REFIID, void**);
// {5AA92D71-4013-463E-BFDE-673DB7C70FCF}
static const CLSID CLSID_FoxSF =
{ 0x5aa92d71, 0x4013, 0x463e, { 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf } };

static PITEMID_CHILD FindChild(IShellFolder* sf, const wchar_t* name, SHCONTF flags,
                               bool ftexSuffix, std::wstring* outName)
{
    IEnumIDList* e = nullptr;
    if (FAILED(sf->EnumObjects(nullptr, flags, &e)) || !e) return nullptr;
    PITEMID_CHILD c = nullptr, found = nullptr; ULONG f = 0;
    while (e->Next(1, &c, &f) == S_OK && f == 1)
    {
        STRRET sr; wchar_t nm[320] = L"";
        if (SUCCEEDED(sf->GetDisplayNameOf(c, SHGDN_NORMAL, &sr))) StrRetToBufW(&sr, c, nm, 320);
        bool hit = ftexSuffix
            ? (wcslen(nm) > 5 && _wcsicmp(nm + wcslen(nm) - 5, L".ftex") == 0)
            : (_wcsicmp(nm, name) == 0);
        if (hit) { found = (PITEMID_CHILD)ILClone(c); if (outName) *outName = nm; CoTaskMemFree(c); break; }
        CoTaskMemFree(c);
    }
    e->Release();
    return found;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 5) { wprintf(L"usage: nse_thumb_test <dll> <archive> <dir> <outPng>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE m = LoadLibraryW(argv[1]);
    auto gco = (pfnGCO)GetProcAddress(m, "DllGetClassObject");
    if (!gco) { wprintf(L"no DllGetClassObject\n"); return 1; }

    PIDLIST_ABSOLUTE pidlFS = nullptr;
    HRESULT hr = SHParseDisplayName(argv[2], nullptr, &pidlFS, 0, nullptr);
    wprintf(L"parse archive hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    IClassFactory* cf = nullptr;
    hr = gco(CLSID_FoxSF, IID_IClassFactory, (void**)&cf);
    if (FAILED(hr)) { wprintf(L"GCO hr=0x%08X\n", hr); return 1; }
    IPersistFolder3* pf = nullptr;
    hr = cf->CreateInstance(nullptr, IID_IPersistFolder3, (void**)&pf); cf->Release();
    if (FAILED(hr)) { wprintf(L"CreateInstance hr=0x%08X\n", hr); return 1; }
    hr = pf->InitializeEx(nullptr, pidlFS, nullptr);
    wprintf(L"InitializeEx hr=0x%08X\n", hr);
    IShellFolder* sf = nullptr; pf->QueryInterface(IID_PPV_ARGS(&sf)); pf->Release();
    if (!sf) { wprintf(L"no IShellFolder\n"); return 1; }

    // Navigate archive -> dir, level by level (our BindToObject).
    std::wstring dir = argv[3]; size_t p = 0;
    while (p < dir.size())
    {
        size_t s = dir.find(L'/', p);
        std::wstring seg = dir.substr(p, s == std::wstring::npos ? std::wstring::npos : s - p);
        p = (s == std::wstring::npos) ? dir.size() : s + 1;
        if (seg.empty()) continue;
        PITEMID_CHILD ch = FindChild(sf, seg.c_str(), SHCONTF_FOLDERS, false, nullptr);
        if (!ch) { wprintf(L"folder '%s' not found\n", seg.c_str()); return 1; }
        IShellFolder* nx = nullptr;
        hr = sf->BindToObject(ch, nullptr, IID_PPV_ARGS(&nx)); CoTaskMemFree(ch); sf->Release();
        if (FAILED(hr)) { wprintf(L"bind '%s' hr=0x%08X\n", seg.c_str(), hr); return 1; }
        sf = nx;
        wprintf(L"  -> %s\n", seg.c_str());
    }

    std::wstring ftexName;
    PITEMID_CHILD ftex = FindChild(sf, nullptr, SHCONTF_NONFOLDERS, true, &ftexName);
    if (!ftex) { wprintf(L"no .ftex in dir\n"); return 1; }
    wprintf(L"ftex item: %s\n", ftexName.c_str());

    // THE TEST: ask the FOLDER for the item's image object (what Explorer does
    // for NSE items). Try IExtractImage (the classic NSE path Explorer uses)
    // first; fall back to IThumbnailProvider. Render whichever we get.
    HBITMAP hb = nullptr; WTS_ALPHATYPE a = WTSAT_UNKNOWN;

    IExtractImage* xi = nullptr;
    hr = sf->GetUIObjectOf(nullptr, 1, (PCUITEMID_CHILD_ARRAY)&ftex,
                           IID_IExtractImage, nullptr, (void**)&xi);
    wprintf(L"GetUIObjectOf(IExtractImage) hr=0x%08X\n", hr);
    if (SUCCEEDED(hr) && xi)
    {
        wchar_t loc[MAX_PATH] = L""; DWORD prio = 0, flags = IEIFLAG_ASYNC;
        SIZE sz = { 256, 256 };
        hr = xi->GetLocation(loc, MAX_PATH, &prio, &sz, 32, &flags);
        wprintf(L"  GetLocation hr=0x%08X flags=0x%X\n", hr, flags);
        hr = xi->Extract(&hb);
        wprintf(L"  Extract hr=0x%08X hbmp=%p\n", hr, (void*)hb);
        xi->Release();
    }

    IThumbnailProvider* tp = nullptr;
    if (!hb)
    {
        hr = sf->GetUIObjectOf(nullptr, 1, (PCUITEMID_CHILD_ARRAY)&ftex,
                               IID_IThumbnailProvider, nullptr, (void**)&tp);
        wprintf(L"GetUIObjectOf(IThumbnailProvider) hr=0x%08X\n", hr);
        if (SUCCEEDED(hr) && tp)
        {
            hr = tp->GetThumbnail(256, &hb, &a);
            wprintf(L"  GetThumbnail hr=0x%08X hbmp=%p\n", hr, (void*)hb);
        }
    }

    {
        if (hb)
        {
            IWICImagingFactory* fac = nullptr;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
            IWICBitmap* bmp = nullptr; fac->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapUseAlpha, &bmp);
            IWICStream* os = nullptr; fac->CreateStream(&os); os->InitializeFromFilename(argv[4], GENERIC_WRITE);
            IWICBitmapEncoder* enc = nullptr; fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
            enc->Initialize(os, WICBitmapEncoderNoCache);
            IWICBitmapFrameEncode* fe = nullptr; enc->CreateNewFrame(&fe, nullptr);
            fe->Initialize(nullptr); fe->WriteSource(bmp, nullptr); fe->Commit(); enc->Commit();
            wprintf(L"PNG -> %s\n", argv[4]);
            DeleteObject(hb);
        }
        else wprintf(L"NO bitmap produced\n");
        if (tp) tp->Release();
    }
    return 0;
}
