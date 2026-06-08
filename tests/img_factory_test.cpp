// Route a loose .ftex through the REAL shell thumbnail pipeline, exactly like
// Explorer's view does for filesystem files: SHCreateItemFromParsingName ->
// IShellItemImageFactory::GetImage(SIIGBF_THUMBNAILONLY). That resolves the
// registered per-extension IThumbnailProvider and runs it (in-process when the
// CLSID has DisableProcessIsolation). If we get a bitmap, the registered
// handler path works; THUMBNAILONLY means no icon fallback masks a failure.
// Usage: img_factory_test.exe <ftexPath> <outPng>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <cstdio>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "gdi32.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: img_factory_test <ftexPath> <outPng>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellItem* item = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(argv[1], nullptr, IID_PPV_ARGS(&item));
    wprintf(L"SHCreateItemFromParsingName hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;

    IShellItemImageFactory* fac = nullptr;
    hr = item->QueryInterface(IID_PPV_ARGS(&fac));
    if (FAILED(hr)) { wprintf(L"QI IShellItemImageFactory hr=0x%08X\n", hr); return 1; }

    SIZE sz = { 256, 256 };
    HBITMAP hb = nullptr;
    hr = fac->GetImage(sz, SIIGBF_THUMBNAILONLY, &hb);
    wprintf(L"GetImage(THUMBNAILONLY) hr=0x%08X hb=%p\n", hr, (void*)hb);
    if (FAILED(hr) || !hb)
    {
        // retry without THUMBNAILONLY to see whether ANYTHING comes back
        hr = fac->GetImage(sz, 0, &hb);
        wprintf(L"GetImage(0) hr=0x%08X hb=%p (may be the generic icon)\n", hr, (void*)hb);
    }
    if (SUCCEEDED(hr) && hb)
    {
        IWICImagingFactory* wf = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wf));
        IWICBitmap* bmp = nullptr; wf->CreateBitmapFromHBITMAP(hb, nullptr, WICBitmapUseAlpha, &bmp);
        IWICStream* os = nullptr; wf->CreateStream(&os); os->InitializeFromFilename(argv[2], GENERIC_WRITE);
        IWICBitmapEncoder* enc = nullptr; wf->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(os, WICBitmapEncoderNoCache);
        IWICBitmapFrameEncode* fe = nullptr; enc->CreateNewFrame(&fe, nullptr);
        fe->Initialize(nullptr); fe->WriteSource(bmp, nullptr); fe->Commit(); enc->Commit();
        wprintf(L"PNG -> %s\n", argv[2]);
        DeleteObject(hb);
    }
    return 0;
}
