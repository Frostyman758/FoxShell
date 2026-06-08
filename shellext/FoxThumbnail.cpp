// FTEX texture thumbnail provider. Explorer hands us a .ftex item; we ask the
// bridge for a small DDS (decoded from the .ftex + its .ftexs sidecars), then
// use WIC to turn that into a 32-bpp HBITMAP for Explorer's thumbnail grid.
//
// We implement IInitializeWithItem (not IInitializeWithStream) because a .ftex
// stream alone lacks the .ftexs sidecars — we need the item's full path so the
// bridge can read the siblings (from the archive, or from the folder on disk).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <strsafe.h>
#include <new>
#include <string>
#include "guid.h"
#include "bridge.h"
#include "dllref.h"

#pragma comment(lib, "windowscodecs.lib")

// DDS bytes -> scaled 32bpp top-down DIB section (HBITMAP), via WIC.
static HRESULT DdsToHBitmap(const BYTE* dds, UINT ddsLen, UINT cx, HBITMAP* outBmp, WTS_ALPHATYPE* outAlpha)
{
    *outBmp = nullptr;
    if (outAlpha) *outAlpha = WTSAT_ARGB;

    IWICImagingFactory* fac = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (FAILED(hr)) return hr;

    IStream* in = SHCreateMemStream(dds, ddsLen);
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICBitmapSource* src = nullptr;

    hr = in ? fac->CreateDecoderFromStream(in, nullptr, WICDecodeMetadataCacheOnDemand, &dec) : E_FAIL;
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);

    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) frame->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (w == 0 || h == 0)) hr = E_FAIL;

    // Fit within cx x cx, preserving aspect.
    UINT dw = w, dh = h;
    if (SUCCEEDED(hr) && (w > cx || h > cx))
    {
        double s = (double)cx / (w >= h ? w : h);
        dw = (UINT)(w * s); if (dw < 1) dw = 1;
        dh = (UINT)(h * s); if (dh < 1) dh = 1;
    }

    if (SUCCEEDED(hr))
    {
        if (dw != w || dh != h)
        {
            hr = fac->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr)) hr = scaler->Initialize(frame, dw, dh, WICBitmapInterpolationModeFant);
            src = scaler;          // alias; owned by `scaler`, released below
        }
        else { src = frame; }      // alias; owned by `frame`, released below
    }

    if (SUCCEEDED(hr)) hr = fac->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA,
                                             WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    if (SUCCEEDED(hr))
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)dw;
        bi.bmiHeader.biHeight = -(LONG)dh;   // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && bits)
        {
            hr = conv->CopyPixels(nullptr, dw * 4, dw * 4 * dh, (BYTE*)bits);
            if (SUCCEEDED(hr)) *outBmp = bmp;
            else DeleteObject(bmp);
        }
        else hr = E_OUTOFMEMORY;
    }

    if (scaler) scaler->Release();   // src is just an alias of frame/scaler — not released separately
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (in) in->Release();
    fac->Release();
    return hr;
}

// Split a full item path into the longest existing real-file prefix (the
// .dat/.fpk/.pftxs on disk) + the interior path within it.
static bool SplitArchivePath(const std::wstring& full, std::wstring& archive, std::wstring& interior)
{
    std::wstring p = full;
    for (auto& c : p) if (c == L'/') c = L'\\';
    size_t idx = p.size();
    while (idx != std::wstring::npos && idx > 0)
    {
        std::wstring prefix = p.substr(0, idx);
        if (PathFileExistsW(prefix.c_str()) && !PathIsDirectoryW(prefix.c_str()))
        {
            archive = prefix;
            interior = (idx < p.size()) ? p.substr(idx + 1) : L"";
            for (auto& c : interior) if (c == L'\\') c = L'/';
            return !interior.empty();
        }
        idx = p.rfind(L'\\', idx - 1);
    }
    return false;
}

// Shared core for both IThumbnailProvider and IExtractImage: take an item's
// full path (loose .ftex on disk, or "<archive>\interior\foo.ftex"), ask the
// bridge for a small DDS, render it to a scaled 32-bpp HBITMAP. All in memory.
static HRESULT FoxRenderThumb(const std::wstring& path, UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pAlpha)
{
    if (!phbmp) return E_POINTER;
    *phbmp = nullptr;
    if (pAlpha) *pAlpha = WTSAT_ARGB;
    if (path.empty()) return E_FAIL;
    if (cx == 0) cx = 256;

    auto& br = Bridge::Get();
    if (!br.EnsureLoaded()) return E_FAIL;

    uint8_t* dds = nullptr; int64_t size = 0; int rc;
    if (PathFileExistsW(path.c_str()))
    {
        if (!br.ftex_thumb_path) return E_FAIL;
        rc = br.ftex_thumb_path(path.c_str(), &dds, &size);
    }
    else
    {
        std::wstring archive, interior;
        if (!SplitArchivePath(path, archive, interior)) return E_FAIL;
        bool owns = false;
        FoxArchive* h = br.OpenChain(archive, {}, owns);
        if (!h || !br.ftex_thumb) return E_FAIL;
        rc = br.ftex_thumb(h, interior.c_str(), &dds, &size);
        br.ReleaseChain(h, owns);
    }
    if (rc != 0 || !dds || size <= 0) { if (dds) br.free_blob(dds); return E_FAIL; }

    HRESULT hr = DdsToHBitmap(dds, (UINT)size, cx, phbmp, pAlpha);
    br.free_blob(dds);
    return hr;
}

class CFoxThumbProvider : public IInitializeWithItem, public IThumbnailProvider
{
public:
    CFoxThumbProvider() { DllAddRef(); }
    ~CFoxThumbProvider() { DllRelease(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IInitializeWithItem)
            *ppv = static_cast<IInitializeWithItem*>(this);
        else if (riid == IID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    // IInitializeWithItem — capture the item's full parse path.
    IFACEMETHODIMP Initialize(IShellItem* item, DWORD) override
    {
        if (!item) return E_INVALIDARG;
        LPWSTR name = nullptr;
        HRESULT hr = item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name);
        if (FAILED(hr)) return hr;
        m_path = name;
        CoTaskMemFree(name);
        return S_OK;
    }

    // Direct path init — used when the NSE folder serves the thumbnail itself
    // (GetUIObjectOf(IThumbnailProvider)): there is no IShellItem to bind, just
    // the item's full archive-qualified path.
    void SetItemPath(const std::wstring& p) { m_path = p; }

    // IThumbnailProvider — get a DDS from the bridge, render to HBITMAP.
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override
    {
        return FoxRenderThumb(m_path, cx, phbmp, pdwAlpha);
    }

private:
    LONG m_ref = 1;
    std::wstring m_path;
};

// Classic IExtractImage path — this is what Explorer's DefView actually asks a
// namespace extension for (via GetUIObjectOf) to thumbnail a virtual item.
// IThumbnailProvider-via-GetUIObjectOf is NOT used for NSE items; IExtractImage
// is. Same in-memory DDS->HBITMAP core.
class CFoxExtractImage : public IExtractImage2
{
public:
    explicit CFoxExtractImage(const std::wstring& path) : m_path(path) { DllAddRef(); }
    ~CFoxExtractImage() { DllRelease(); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IExtractImage)
            *ppv = static_cast<IExtractImage*>(this);
        else if (riid == IID_IExtractImage2)
            *ppv = static_cast<IExtractImage2*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    // IExtractImage — Explorer first calls GetLocation (telling us the desired
    // size + handing back a cache key), then Extract to get the bitmap.
    IFACEMETHODIMP GetLocation(LPWSTR pszPathBuffer, DWORD cch, DWORD* pdwPriority,
                               const SIZE* prgSize, DWORD /*dwRecClrDepth*/, DWORD* pdwFlags) override
    {
        if (prgSize) { m_cx = (UINT)max(prgSize->cx, prgSize->cy); if (m_cx == 0) m_cx = 256; }
        if (pdwPriority) *pdwPriority = 0;
        if (pdwFlags) *pdwFlags &= ~IEIFLAG_ASYNC;     // we extract synchronously
        // The buffer is a cache key; the item's full path is naturally unique.
        if (pszPathBuffer && cch) StringCchCopyW(pszPathBuffer, cch, m_path.c_str());
        return S_OK;
    }
    IFACEMETHODIMP Extract(HBITMAP* phBmpImage) override
    {
        return FoxRenderThumb(m_path, m_cx, phBmpImage, nullptr);
    }
    // IExtractImage2
    IFACEMETHODIMP GetDateStamp(FILETIME* pDateStamp) override
    {
        if (pDateStamp) { pDateStamp->dwLowDateTime = 0; pDateStamp->dwHighDateTime = 0; }
        return S_OK;
    }

private:
    LONG m_ref = 1;
    std::wstring m_path;
    UINT m_cx = 256;
};

HRESULT CreateFoxThumbProvider(REFIID riid, void** ppv)
{
    auto* p = new (std::nothrow) CFoxThumbProvider();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

// Used by FoxShellFolder::GetUIObjectOf to serve thumbnails for virtual NSE
// items. Explorer asks the folder for IExtractImage (classic NSE thumbnail
// path); we also answer IThumbnailProvider for callers that use it. Dispatch by
// the requested interface so we hand back an object that actually implements it.
HRESULT CreateFoxThumbProviderForPath(const wchar_t* fullPath, REFIID riid, void** ppv)
{
    if (ppv) *ppv = nullptr;
    if (!fullPath) return E_INVALIDARG;

    if (riid == IID_IExtractImage || riid == IID_IExtractImage2)
    {
        auto* p = new (std::nothrow) CFoxExtractImage(fullPath);
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    auto* p = new (std::nothrow) CFoxThumbProvider();
    if (!p) return E_OUTOFMEMORY;
    p->SetItemPath(fullPath);
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}
