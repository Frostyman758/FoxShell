#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <new>
#include "FoxDataObject.h"
#include "pidl.h"
#include "bridge.h"
#include "dllref.h"
#include "foxlog.h"

#pragma comment(lib, "shlwapi.lib")

namespace {

// One leaf file to extract: where it lives inside the (innermost) archive, the
// relative path to write at the drop target, and its size.
struct DragFile
{
    std::wstring rel;       // descriptor path, backslash-separated (e.g. L"sub\\a.bin")
    std::wstring interior;  // archive interior path, forward-slash (e.g. L"dir/sub/a.bin")
    ULONGLONG    size;
};

// Clipboard formats we offer (registered once).
CLIPFORMAT g_cfDesc = 0, g_cfContents = 0, g_cfDropEffect = 0;
void EnsureFormats()
{
    if (!g_cfDesc)       g_cfDesc       = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    if (!g_cfContents)   g_cfContents   = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTS);
    if (!g_cfDropEffect) g_cfDropEffect = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
}

// Recursively list a directory inside the archive, appending every leaf file.
void CollectDir(Bridge& br, FoxArchive* h,
                const std::wstring& interiorDir, const std::wstring& relPrefix,
                std::vector<DragFile>& out)
{
    FoxList* list = nullptr;
    if (br.list(h, interiorDir.c_str(), &list) != FOXARC_OK) return;
    int n = 0; br.list_count(list, &n);
    for (int i = 0; i < n; i++)
    {
        FoxItemInfo info{};
        if (br.list_item(list, i, &info) != FOXARC_OK) continue;
        std::wstring childInterior = interiorDir.empty()
            ? std::wstring(info.name) : interiorDir + L"/" + info.name;
        std::wstring childRel = relPrefix.empty()
            ? std::wstring(info.name) : relPrefix + L"\\" + info.name;
        if (info.isFolder)                                  // recurse into folders
            CollectDir(br, h, childInterior, childRel, out);
        else                                                // file or nested archive = leaf
            out.push_back({ childRel, childInterior, info.size });
    }
    br.list_free(list);
}

class CFoxDataObject : public IDataObject
{
public:
    CFoxDataObject(std::wstring archivePath, std::vector<std::wstring> chain,
                   std::vector<DragFile> files)
        : m_archivePath(std::move(archivePath)), m_chain(std::move(chain)),
          m_files(std::move(files))
    { EnsureFormats(); DllAddRef(); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject)
        { *ppv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    // IDataObject
    IFACEMETHODIMP GetData(FORMATETC* p, STGMEDIUM* m) override
    {
        if (!p || !m) return E_INVALIDARG;
        ZeroMemory(m, sizeof(*m));

        if (p->cfFormat == g_cfDesc && (p->tymed & TYMED_HGLOBAL))
        {
            HGLOBAL hg = BuildDescriptor();
            if (!hg) return E_OUTOFMEMORY;
            m->tymed = TYMED_HGLOBAL; m->hGlobal = hg;
            return S_OK;
        }
        if (p->cfFormat == g_cfContents && (p->tymed & TYMED_ISTREAM))
        {
            if (p->lindex < 0 || (size_t)p->lindex >= m_files.size()) return DV_E_LINDEX;
            IStream* s = ReadStream((size_t)p->lindex);
            if (!s) return E_FAIL;
            m->tymed = TYMED_ISTREAM; m->pstm = s;
            return S_OK;
        }
        if (p->cfFormat == g_cfDropEffect && (p->tymed & TYMED_HGLOBAL))
        {
            HGLOBAL hg = GlobalAlloc(GHND, sizeof(DWORD));
            if (!hg) return E_OUTOFMEMORY;
            *static_cast<DWORD*>(GlobalLock(hg)) = DROPEFFECT_COPY;
            GlobalUnlock(hg);
            m->tymed = TYMED_HGLOBAL; m->hGlobal = hg;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

    IFACEMETHODIMP QueryGetData(FORMATETC* p) override
    {
        if (!p) return E_INVALIDARG;
        if ((p->cfFormat == g_cfDesc       && (p->tymed & TYMED_HGLOBAL)) ||
            (p->cfFormat == g_cfContents   && (p->tymed & TYMED_ISTREAM)) ||
            (p->cfFormat == g_cfDropEffect && (p->tymed & TYMED_HGLOBAL)))
            return S_OK;
        return DV_E_FORMATETC;
    }

    IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
    { if (out) { *out = {}; out->ptd = nullptr; } return DATA_S_SAMEFORMATETC; }

    IFACEMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }

    IFACEMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** pp) override
    {
        if (dir != DATADIR_GET) { if (pp) *pp = nullptr; return E_NOTIMPL; }
        FORMATETC f[3] = {
            { g_cfDesc,       nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
            { g_cfContents,   nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM },
            { g_cfDropEffect, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
        };
        return SHCreateStdEnumFmtEtc(3, f, pp);
    }

    IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    ~CFoxDataObject() { DllRelease(); }

    // FILEGROUPDESCRIPTORW { UINT cItems; FILEDESCRIPTORW fgd[cItems]; }
    HGLOBAL BuildDescriptor()
    {
        size_t count = m_files.size();
        SIZE_T cb = sizeof(UINT) + count * sizeof(FILEDESCRIPTORW);
        HGLOBAL hg = GlobalAlloc(GHND, cb);
        if (!hg) return nullptr;
        auto* fgd = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(hg));
        fgd->cItems = (UINT)count;
        for (size_t i = 0; i < count; i++)
        {
            FILEDESCRIPTORW& fd = fgd->fgd[i];
            fd.dwFlags = FD_FILESIZE | FD_ATTRIBUTES | FD_PROGRESSUI;
            fd.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            fd.nFileSizeLow  = (DWORD)(m_files[i].size & 0xFFFFFFFFu);
            fd.nFileSizeHigh = (DWORD)(m_files[i].size >> 32);
            lstrcpynW(fd.cFileName, m_files[i].rel.c_str(), MAX_PATH);
        }
        GlobalUnlock(hg);
        return hg;
    }

    // Pull one file's bytes from the archive into a memory stream (copy, so the
    // bridge blob is released immediately — matches the browse-time policy).
    IStream* ReadStream(size_t idx)
    {
        auto& br = Bridge::Get();
        bool owns = false;
        FoxArchive* h = br.OpenChain(m_archivePath, m_chain, owns);
        if (!h) return nullptr;
        uint8_t* data = nullptr; int64_t size = 0;
        int rc = br.read(h, m_files[idx].interior.c_str(), &data, &size);
        br.ReleaseChain(h, owns);
        if (rc != FOXARC_OK) return nullptr;
        IStream* s = SHCreateMemStream(data, (UINT)size);
        br.free_blob(data);
        return s;
    }

    LONG m_ref = 1;
    std::wstring m_archivePath;
    std::vector<std::wstring> m_chain;
    std::vector<DragFile> m_files;
};

} // namespace

HRESULT CreateFoxDataObject(const std::wstring& archivePath,
                            const std::vector<std::wstring>& chain,
                            const std::wstring& dirPath,
                            UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                            REFIID riid, void** ppv)
{
    if (ppv) *ppv = nullptr;
    if (cidl == 0 || !apidl) return E_INVALIDARG;

    // Flatten the selection into leaf files (recursing into selected folders).
    std::vector<DragFile> files;
    {
        auto& br = Bridge::Get();
        bool owns = false;
        FoxArchive* h = br.OpenChain(archivePath, chain, owns);
        if (!h) return E_FAIL;
        for (UINT i = 0; i < cidl; i++)
        {
            const FoxItemID* f = FoxFromItem(apidl[i]);
            if (!f) continue;
            std::wstring interior = dirPath.empty()
                ? std::wstring(f->name) : dirPath + L"/" + f->name;
            if (f->kind == FOX_FOLDER)
                CollectDir(br, h, interior, f->name, files);
            else
                files.push_back({ f->name, interior, f->size });
        }
        br.ReleaseChain(h, owns);
    }

    auto* obj = new (std::nothrow) CFoxDataObject(archivePath, chain, std::move(files));
    if (!obj) return E_OUTOFMEMORY;
    HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}
