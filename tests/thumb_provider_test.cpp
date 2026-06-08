// Drive the FTEX IThumbnailProvider exactly like Explorer would: create it via
// the class factory, IInitializeWithItem with a .ftex IShellItem, GetThumbnail,
// then save the HBITMAP as PNG. No registry needed.
// Usage: thumb_provider_test.exe <foxshellext.dll> <ftexPath> <outPng>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <cstdio>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")

typedef HRESULT (__stdcall *pfnGCO)(REFCLSID, REFIID, void**);
// {A57C06D6-E969-484F-A708-9B73BF3B861D}
static const CLSID CLSID_FoxThumb =
{ 0xa57c06d6, 0xe969, 0x484f, { 0xa7, 0x08, 0x9b, 0x73, 0xbf, 0x3b, 0x86, 0x1d } };

static HRESULT SaveHBitmapPng(HBITMAP hbmp, const wchar_t* path)
{
    IWICImagingFactory* fac = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    IWICBitmap* bmp = nullptr;
    HRESULT hr = fac->CreateBitmapFromHBITMAP(hbmp, nullptr, WICBitmapUseAlpha, &bmp);
    if (FAILED(hr)) return hr;
    IWICStream* os = nullptr; fac->CreateStream(&os); os->InitializeFromFilename(path, GENERIC_WRITE);
    IWICBitmapEncoder* enc = nullptr; fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(os, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* fe = nullptr; enc->CreateNewFrame(&fe, nullptr);
    fe->Initialize(nullptr);
    hr = fe->WriteSource(bmp, nullptr);
    fe->Commit(); enc->Commit();
    return hr;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) { wprintf(L"usage: thumb_provider_test <dll> <ftex> <outPng>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE m = LoadLibraryW(argv[1]);
    auto gco = (pfnGCO)GetProcAddress(m, "DllGetClassObject");
    if (!gco) { wprintf(L"no DllGetClassObject\n"); return 1; }

    IClassFactory* cf = nullptr;
    HRESULT hr = gco(CLSID_FoxThumb, IID_IClassFactory, (void**)&cf);
    if (FAILED(hr)) { wprintf(L"GCO thumb hr=0x%08X\n", hr); return 1; }
    IInitializeWithItem* init = nullptr;
    hr = cf->CreateInstance(nullptr, IID_IInitializeWithItem, (void**)&init);
    cf->Release();
    if (FAILED(hr)) { wprintf(L"CreateInstance IInitializeWithItem hr=0x%08X\n", hr); return 1; }

    IShellItem* item = nullptr;
    hr = SHCreateItemFromParsingName(argv[2], nullptr, IID_PPV_ARGS(&item));
    wprintf(L"SHCreateItemFromParsingName hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;
    hr = init->Initialize(item, 0);
    wprintf(L"Initialize hr=0x%08X\n", hr);

    IThumbnailProvider* tp = nullptr;
    hr = init->QueryInterface(IID_PPV_ARGS(&tp));
    HBITMAP hbmp = nullptr; WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    hr = tp->GetThumbnail(256, &hbmp, &alpha);
    wprintf(L"GetThumbnail hr=0x%08X hbmp=%p alpha=%d\n", hr, (void*)hbmp, (int)alpha);
    if (SUCCEEDED(hr) && hbmp)
    {
        hr = SaveHBitmapPng(hbmp, argv[3]);
        wprintf(L"saved PNG hr=0x%08X -> %s\n", hr, argv[3]);
        DeleteObject(hbmp);
    }
    return 0;
}
