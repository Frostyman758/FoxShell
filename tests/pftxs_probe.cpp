// Open a .pftxs (or any archive) as the root, recursively list it, and for the
// first .ftex found call foxarc_ftex_thumb -> report DDS size + dims. Proves
// whether the "pftxs is the archive" path works and whether pftxs holds
// .ftex+.ftexs sibling pairs.
// Usage: pftxs_probe.exe <foxarchive.dll> <dictDir> <archivePath> [outPng]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

typedef void* FArc; typedef void* Lst;
struct ItemInfo { const wchar_t* name; int32_t isFolder; int32_t isFArchive; uint64_t size; uint64_t hash; };

typedef int32_t (*pfn_open)(const wchar_t*, FArc*);
typedef void    (*pfn_close)(FArc);
typedef int32_t (*pfn_list)(FArc, const wchar_t*, Lst*);
typedef int32_t (*pfn_lcount)(Lst, int32_t*);
typedef int32_t (*pfn_litem)(Lst, int32_t, ItemInfo*);
typedef void    (*pfn_lfree)(Lst);
typedef int32_t (*pfn_thumb)(FArc, const wchar_t*, uint8_t**, int64_t*);
typedef void    (*pfn_free)(uint8_t*);
typedef void    (*pfn_setdict)(const wchar_t*);

static pfn_open  g_open;  static pfn_close g_close; static pfn_list g_list;
static pfn_lcount g_count; static pfn_litem g_item; static pfn_lfree g_lfree;

static std::wstring g_firstFtex;
static int g_ftexCount = 0, g_ftexsCount = 0;

static void Walk(FArc a, const std::wstring& dir, int depth)
{
    Lst lst = nullptr;
    if (g_list(a, dir.c_str(), &lst) != 0) return;
    int32_t n = 0; g_count(lst, &n);
    for (int i = 0; i < n; i++)
    {
        ItemInfo it{};
        if (g_item(lst, i, &it) != 0) continue;
        std::wstring child = dir.empty() ? it.name : dir + L"/" + it.name;
        if (it.isFolder)
            Walk(a, child, depth + 1);
        else
        {
            std::wstring nm = it.name;
            if (nm.size() > 5 && _wcsicmp(nm.c_str() + nm.size() - 5, L".ftex") == 0)
            {
                if (g_ftexCount < 3) wprintf(L"  .ftex : %s (%llu B)\n", child.c_str(), (unsigned long long)it.size);
                if (g_firstFtex.empty()) g_firstFtex = child;
                g_ftexCount++;
            }
            else if (nm.size() > 6 && _wcsicmp(nm.c_str() + nm.size() - 6, L".ftexs") == 0)
            {
                if (g_ftexsCount < 3) wprintf(L"  .ftexs: %s (%llu B)\n", child.c_str(), (unsigned long long)it.size);
                g_ftexsCount++;
            }
        }
    }
    g_lfree(lst);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) { wprintf(L"usage: pftxs_probe <dll> <dictDir> <archive> [outPng]\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE m = LoadLibraryW(argv[1]);
    auto setDict = (pfn_setdict)GetProcAddress(m, "foxarc_set_dict_dir");
    g_open  = (pfn_open)GetProcAddress(m, "foxarc_open");
    g_close = (pfn_close)GetProcAddress(m, "foxarc_close");
    g_list  = (pfn_list)GetProcAddress(m, "foxarc_list");
    g_count = (pfn_lcount)GetProcAddress(m, "foxarc_list_count");
    g_item  = (pfn_litem)GetProcAddress(m, "foxarc_list_item");
    g_lfree = (pfn_lfree)GetProcAddress(m, "foxarc_list_free");
    auto thumb = (pfn_thumb)GetProcAddress(m, "foxarc_ftex_thumb");
    auto freeb = (pfn_free)GetProcAddress(m, "foxarc_free_blob");
    if (setDict) setDict(argv[2]);

    FArc a = nullptr;
    int rc = g_open(argv[3], &a);
    wprintf(L"foxarc_open rc=%d\n", rc);
    if (rc != 0) return 1;

    Walk(a, L"", 0);
    wprintf(L"TOTAL .ftex=%d  .ftexs=%d\n", g_ftexCount, g_ftexsCount);

    // Optional 5th arg: explicit interior .ftex path to thumb (may cross a
    // nested archive boundary, e.g. "x/y.pftxs/Assets/.../z.ftex").
    if (argc >= 6) g_firstFtex = argv[5];

    if (!g_firstFtex.empty() && thumb)
    {
        uint8_t* dds = nullptr; int64_t size = 0;
        rc = thumb(a, g_firstFtex.c_str(), &dds, &size);
        wprintf(L"foxarc_ftex_thumb('%s') rc=%d ddsSize=%lld\n", g_firstFtex.c_str(), rc, (long long)size);
        if (rc == 0 && argc >= 5)
        {
            IWICImagingFactory* fac = nullptr;
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
            IStream* in = SHCreateMemStream(dds, (UINT)size);
            IWICBitmapDecoder* dec = nullptr;
            HRESULT hr = fac->CreateDecoderFromStream(in, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
            if (SUCCEEDED(hr)) {
                IWICBitmapFrameDecode* frame = nullptr; dec->GetFrame(0, &frame);
                UINT w=0,h=0; frame->GetSize(&w,&h);
                wprintf(L"decoded frame: %ux%u\n", w, h);
                IWICFormatConverter* conv=nullptr; fac->CreateFormatConverter(&conv);
                conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
                IWICStream* os=nullptr; fac->CreateStream(&os); os->InitializeFromFilename(argv[4], GENERIC_WRITE);
                IWICBitmapEncoder* enc=nullptr; fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
                enc->Initialize(os, WICBitmapEncoderNoCache);
                IWICBitmapFrameEncode* fe=nullptr; enc->CreateNewFrame(&fe, nullptr);
                fe->Initialize(nullptr); fe->SetSize(w,h);
                WICPixelFormatGUID pf=GUID_WICPixelFormat32bppPBGRA; fe->SetPixelFormat(&pf);
                fe->WriteSource(conv, nullptr); fe->Commit(); enc->Commit();
                wprintf(L"PNG -> %s\n", argv[4]);
            } else wprintf(L"WIC decode hr=0x%08X\n", hr);
        }
        if (dds) freeb(dds);
    }
    g_close(a);
    return 0;
}
