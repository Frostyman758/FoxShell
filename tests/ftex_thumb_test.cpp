// foxarc_ftex_thumb_path(.ftex) -> DDS -> WIC decode -> PNG, to validate the
// whole thumbnail chain (and the exact WIC path the provider will use).
// Usage: ftex_thumb_test.exe <foxarchive.dll> <dictDir> <ftexPath> <outPng>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdint>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

typedef int32_t (*pfn_thumb)(const wchar_t*, uint8_t**, int64_t*);
typedef void    (*pfn_free)(uint8_t*);
typedef void    (*pfn_setdict)(const wchar_t*);

int wmain(int argc, wchar_t** argv)
{
    if (argc < 5) { wprintf(L"usage: ftex_thumb_test <dll> <dictDir> <ftex> <outPng>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE m = LoadLibraryW(argv[1]);
    auto setDict = (pfn_setdict)GetProcAddress(m, "foxarc_set_dict_dir");
    auto thumb   = (pfn_thumb)GetProcAddress(m, "foxarc_ftex_thumb_path");
    auto freeb   = (pfn_free)GetProcAddress(m, "foxarc_free_blob");
    if (!thumb) { wprintf(L"no foxarc_ftex_thumb_path export\n"); return 1; }
    if (setDict) setDict(argv[2]);

    uint8_t* dds = nullptr; int64_t size = 0;
    int rc = thumb(argv[3], &dds, &size);
    wprintf(L"foxarc_ftex_thumb_path rc=%d ddsSize=%lld magic=%c%c%c%c\n", rc, (long long)size,
            size > 3 ? dds[0] : '?', size > 3 ? dds[1] : '?', size > 3 ? dds[2] : '?', size > 3 ? dds[3] : '?');
    if (rc != 0) return 1;

    // DDS bytes -> WIC -> 32bppPBGRA -> PNG
    IWICImagingFactory* fac = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    IStream* in = SHCreateMemStream(dds, (UINT)size);
    IWICBitmapDecoder* dec = nullptr;
    HRESULT hr = fac->CreateDecoderFromStream(in, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    wprintf(L"WIC CreateDecoderFromStream hr=0x%08X\n", hr);
    if (FAILED(hr)) return 1;
    IWICBitmapFrameDecode* frame = nullptr; dec->GetFrame(0, &frame);
    UINT w = 0, h = 0; frame->GetSize(&w, &h);
    wprintf(L"decoded frame: %ux%u\n", w, h);

    IWICFormatConverter* conv = nullptr; fac->CreateFormatConverter(&conv);
    conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    IWICStream* os = nullptr; fac->CreateStream(&os); os->InitializeFromFilename(argv[4], GENERIC_WRITE);
    IWICBitmapEncoder* enc = nullptr; fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(os, WICBitmapEncoderNoCache);
    IWICBitmapFrameEncode* fe = nullptr; enc->CreateNewFrame(&fe, nullptr);
    fe->Initialize(nullptr); fe->SetSize(w, h);
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppPBGRA; fe->SetPixelFormat(&pf);
    hr = fe->WriteSource(conv, nullptr);
    fe->Commit(); enc->Commit();
    wprintf(L"PNG written hr=0x%08X -> %s\n", hr, argv[4]);

    freeb(dds);
    return 0;
}
