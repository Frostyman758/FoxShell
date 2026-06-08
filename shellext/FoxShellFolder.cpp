#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <new>
#include "guid.h"
#include "pidl.h"
#include "bridge.h"
#include "../include/foxmagic.h"
#include "../include/foxtypes.h"
#include "FoxShellFolder.h"
#include "FoxContextMenu.h"
#include "FoxEnumIDList.h"
#include "FoxDataObject.h"
#include "dllref.h"
#include "foxlog.h"

#pragma comment(lib, "shlwapi.lib")

// FoxThumbnail.cpp — make our FTEX thumbnail provider for a virtual item path.
HRESULT CreateFoxThumbProviderForPath(const wchar_t* fullPath, REFIID riid, void** ppv);

static LPCITEMIDLIST NextRaw(LPCITEMIDLIST p)
{ return reinterpret_cast<LPCITEMIDLIST>(reinterpret_cast<const BYTE*>(p) + p->mkid.cb); }

// Clone a single SHITEMID (our FoxItemID) into a 1-item relative PIDL.
static LPITEMIDLIST CloneSingle(const FoxItemID* f)
{
    size_t total = f->cb + sizeof(USHORT);
    auto* p = static_cast<BYTE*>(CoTaskMemAlloc(total));
    if (!p) return nullptr;
    ZeroMemory(p, total);
    memcpy(p, f, f->cb);
    return reinterpret_cast<LPITEMIDLIST>(p);
}

// ── FoxShellFolder ──────────────────────────────────────────────────────────

FoxShellFolder::FoxShellFolder() { DllAddRef(); }

FoxShellFolder::~FoxShellFolder()
{
    if (m_pidlAbs) CoTaskMemFree(m_pidlAbs);
    DllRelease();
}

std::wstring FoxShellFolder::ChildInteriorPath(const wchar_t* name) const
{
    return m_dirPath.empty() ? std::wstring(name) : m_dirPath + L"/" + name;
}

// Derive m_archivePath / m_chain / m_dirPath from the absolute PIDL: the
// filesystem prefix names the .dat; trailing FoxItemIDs give chain + dir.
static void RebuildState(PCIDLIST_ABSOLUTE pidlAbs, std::wstring& archivePath,
                         std::vector<std::wstring>& chain, std::wstring& dir)
{
    archivePath.clear(); chain.clear(); dir.clear();
    if (!pidlAbs) return;

    // Find the first FoxItemID in the chain.
    const FoxItemID* firstFox = nullptr;
    size_t firstOff = 0;
    for (LPCITEMIDLIST it = pidlAbs; it && it->mkid.cb; it = NextRaw(it))
    {
        if (auto* f = FoxFromItem(it))
        {
            firstFox = f;
            firstOff = reinterpret_cast<const BYTE*>(it) - reinterpret_cast<const BYTE*>(pidlAbs);
            break;
        }
    }

    // Build a filesystem-only PIDL (truncate at the first fox item) -> .dat path.
    LPITEMIDLIST fsClone = ILCloneFull(pidlAbs);
    if (fsClone)
    {
        if (firstFox)
            *reinterpret_cast<USHORT*>(reinterpret_cast<BYTE*>(fsClone) + firstOff) = 0;
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(fsClone, path)) archivePath = path;
        ILFree(fsClone);
    }

    // Walk trailing fox items into chain + dir.
    if (firstFox)
    {
        for (LPCITEMIDLIST it = reinterpret_cast<LPCITEMIDLIST>(
                 reinterpret_cast<const BYTE*>(pidlAbs) + firstOff);
             it && it->mkid.cb; it = NextRaw(it))
        {
            const FoxItemID* f = FoxFromItem(it);
            if (!f) break;
            if (f->kind == FOX_ARCHIVE)
            {
                chain.push_back(dir.empty() ? std::wstring(f->name) : dir + L"/" + f->name);
                dir.clear();
            }
            else // folder
            {
                dir = dir.empty() ? std::wstring(f->name) : dir + L"/" + f->name;
            }
        }
    }
}

HRESULT FoxShellFolder::InitAsChild(FoxShellFolder* parent, const FoxItemID* item)
{
    LPITEMIDLIST rel = CloneSingle(item);
    if (!rel) return E_OUTOFMEMORY;
    m_pidlAbs = ILCombine(parent->m_pidlAbs, rel);
    ILFree(rel);
    if (!m_pidlAbs) return E_OUTOFMEMORY;
    RebuildState(m_pidlAbs, m_archivePath, m_chain, m_dirPath);
    return S_OK;
}

// IUnknown
IFACEMETHODIMP FoxShellFolder::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IShellFolder || riid == IID_IShellFolder2)
        *ppv = static_cast<IShellFolder2*>(this);
    else if (riid == IID_IPersist || riid == IID_IPersistFolder ||
             riid == IID_IPersistFolder2 || riid == IID_IPersistFolder3)
        *ppv = static_cast<IPersistFolder3*>(this);
    else
    {
        FoxLogIID("QI reject", riid);
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) FoxShellFolder::AddRef() { return InterlockedIncrement(&m_ref); }
IFACEMETHODIMP_(ULONG) FoxShellFolder::Release()
{
    LONG r = InterlockedDecrement(&m_ref);
    if (r == 0) delete this;
    return r;
}

// IPersist*
IFACEMETHODIMP FoxShellFolder::GetClassID(CLSID* p) { if (!p) return E_POINTER; *p = CLSID_FoxShellFolder; return S_OK; }

IFACEMETHODIMP FoxShellFolder::Initialize(PCIDLIST_ABSOLUTE pidl)
{
    if (m_pidlAbs) { CoTaskMemFree(m_pidlAbs); m_pidlAbs = nullptr; }
    m_pidlAbs = ILCloneFull(pidl);
    if (!m_pidlAbs) return E_OUTOFMEMORY;
    RebuildState(m_pidlAbs, m_archivePath, m_chain, m_dirPath);
    { char a[MAX_PATH]; WideCharToMultiByte(CP_ACP,0,m_archivePath.c_str(),-1,a,MAX_PATH,0,0);
      FoxLog("Initialize archivePath='%s' chain=%zu dir='%ls'", a, m_chain.size(), m_dirPath.c_str()); }
    return S_OK;
}

IFACEMETHODIMP FoxShellFolder::GetCurFolder(PIDLIST_ABSOLUTE* ppidl)
{
    if (!ppidl) return E_POINTER;
    *ppidl = m_pidlAbs ? ILCloneFull(m_pidlAbs) : nullptr;
    return *ppidl ? S_OK : E_FAIL;
}

IFACEMETHODIMP FoxShellFolder::InitializeEx(IBindCtx*, PCIDLIST_ABSOLUTE pidlRoot,
                                            const PERSIST_FOLDER_TARGET_INFO*)
{ return Initialize(pidlRoot); }

IFACEMETHODIMP FoxShellFolder::GetFolderTargetInfo(PERSIST_FOLDER_TARGET_INFO*) { return E_NOTIMPL; }

// IShellFolder
IFACEMETHODIMP FoxShellFolder::EnumObjects(HWND, SHCONTF flags, IEnumIDList** ppenum)
{
    if (!ppenum) return E_POINTER;
    *ppenum = nullptr;
    FoxLog("EnumObjects dir='%ls' flags=0x%X", m_dirPath.c_str(), (unsigned)flags);

    // Content gate: only enumerate a top-level file that is genuinely a Fox
    // archive. A generic .dat (save game, video, other app's data) sniffs as
    // Unknown, so we return an empty enumerator and the shell shows nothing
    // rather than a broken folder. (Nested levels always have a real archive.)
    if (m_chain.empty() && !m_archivePath.empty() && !FoxIsArchiveFile(m_archivePath.c_str()))
    {
        FoxLog("EnumObjects: '%ls' is not a Fox archive — declining", m_archivePath.c_str());
        *ppenum = new (std::nothrow) FoxEnumIDList(std::vector<PITEMID_CHILD>{});
        return *ppenum ? S_OK : E_OUTOFMEMORY;
    }

    auto& br = Bridge::Get();
    bool owns = false;
    FoxArchive* h = br.OpenChain(m_archivePath, m_chain, owns);
    if (!h) { FoxLog("EnumObjects: OpenChain FAILED"); return E_FAIL; }

    FoxList* list = nullptr;
    int rc = br.list(h, m_dirPath.c_str(), &list);
    if (rc != FOXARC_OK) { br.ReleaseChain(h, owns); return E_FAIL; }

    int n = 0; br.list_count(list, &n);
    std::vector<PITEMID_CHILD> items;
    items.reserve(n);
    for (int i = 0; i < n; i++)
    {
        FoxItemInfo info{};
        if (br.list_item(list, i, &info) != FOXARC_OK) continue;
        bool folderish = info.isFolder || info.isArchive;
        if (folderish && !(flags & SHCONTF_FOLDERS)) continue;
        if (!folderish && !(flags & SHCONTF_NONFOLDERS)) continue;
        FoxKind k = info.isArchive ? FOX_ARCHIVE : info.isFolder ? FOX_FOLDER : FOX_FILE;
        if (auto* p = FoxCreateItem(k, info.name, info.size, info.pathHash))
            items.push_back(reinterpret_cast<PITEMID_CHILD>(p));
    }
    br.list_free(list);
    br.ReleaseChain(h, owns);

    auto* e = new (std::nothrow) FoxEnumIDList(std::move(items));
    if (!e) return E_OUTOFMEMORY;
    *ppenum = e;
    return S_OK;
}

IFACEMETHODIMP FoxShellFolder::BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv)
{
    if (ppv) *ppv = nullptr;
    const FoxItemID* f = FoxFromItem(pidl);
#ifdef FOX_LOG
    { wchar_t g[64]; StringFromGUID2(riid, g, 64);
      FoxLog("BindToObject name='%ls' iid=%ls", f ? f->name : L"(not-ours)", g); }
#endif
    if (!f) return E_INVALIDARG;

    // A file's content stream — this is how the shell's Copy / drag-out pulls
    // the bytes out to a real folder. Read on demand, hand the shell a memory
    // stream, and release our copy immediately.
    if (riid == IID_IStream && f->kind == FOX_FILE && !FoxNext(pidl))
    {
        std::wstring interior = ChildInteriorPath(f->name);
        auto& br = Bridge::Get();
        bool owns = false;
        FoxArchive* h = br.OpenChain(m_archivePath, m_chain, owns);
        if (!h) return E_FAIL;
        uint8_t* data = nullptr; int64_t size = 0;
        int rc = br.read(h, interior.c_str(), &data, &size);
        br.ReleaseChain(h, owns);
        if (rc != FOXARC_OK) return E_FAIL;
        IStream* s = SHCreateMemStream(data, (UINT)size);
        br.free_blob(data);
        if (!s) return E_OUTOFMEMORY;
        *ppv = s;
        return S_OK;
    }

    auto* child = new (std::nothrow) FoxShellFolder();
    if (!child) return E_OUTOFMEMORY;
    HRESULT hr = child->InitAsChild(this, f);
    if (FAILED(hr)) { child->Release(); return hr; }

    LPCITEMIDLIST rest = FoxNext(pidl);
    if (rest) hr = child->BindToObject(rest, pbc, riid, ppv);
    else      hr = child->QueryInterface(riid, ppv);
    child->Release();
    return hr;
}

IFACEMETHODIMP FoxShellFolder::BindToStorage(PCUIDLIST_RELATIVE, IBindCtx*, REFIID, void** ppv)
{ if (ppv) *ppv = nullptr; return E_NOTIMPL; }

IFACEMETHODIMP FoxShellFolder::CompareIDs(LPARAM, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2)
{
    const FoxItemID* a = FoxFromItem(pidl1);
    const FoxItemID* b = FoxFromItem(pidl2);
    if (!a || !b) return E_INVALIDARG;

    int c;
    bool da = a->kind != FOX_FILE, db = b->kind != FOX_FILE;
    if (da != db) c = da ? -1 : 1;
    else c = CompareStringOrdinal(a->name, -1, b->name, -1, TRUE) - CSTR_EQUAL;

    if (c == 0)
    {
        LPCITEMIDLIST n1 = FoxNext(pidl1), n2 = FoxNext(pidl2);
        if (n1 && n2) return CompareIDs(0, n1, n2);
        if (n1) c = 1; else if (n2) c = -1;
    }
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (USHORT)(SHORT)c);
}

IFACEMETHODIMP FoxShellFolder::CreateViewObject(HWND hwnd, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    FoxLogIID("CreateViewObject", riid);
    if (riid == IID_IShellView)
    {
        SFV_CREATE csfv = { sizeof(csfv) };
        csfv.pshf = static_cast<IShellFolder*>(this);
        HRESULT hr = SHCreateShellFolderView(&csfv, reinterpret_cast<IShellView**>(ppv));
        FoxLog("CreateViewObject SHCreateShellFolderView hr=0x%08X", hr);
        return hr;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP FoxShellFolder::GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF* rgf)
{
    if (!rgf) return E_POINTER;
    // NOTE: do NOT advertise SFGAO_BROWSABLE on folder items. That bit tells the
    // shell to host the item in-place like a web document, which makes Explorer
    // render our sub-folders with a blank "document" icon and ignore double-click
    // instead of navigating into them. Plain folders use FOLDER|HASSUBFOLDER.
    SFGAOF inMask = *rgf;
    SFGAOF acc = (SFGAOF)~0u;
    // SFGAO_STORAGEANCESTOR ("may contain SFGAO_STORAGE/SFGAO_STREAM children")
    // is what tells Explorer's search engine to RECURSE into a folder. Without
    // it the search box only scans the current directory; with it (as zipfldr
    // reports) the slow-search descends through the whole archive tree.
    if (cidl == 0)
    {
        // Attributes of the junction folder itself. Content gate: if the backing
        // file isn't a real Fox archive, drop FOLDER/HASSUBFOLDER so the shell
        // doesn't offer the expand chevron on a generic .dat.
        if (m_chain.empty() && !m_archivePath.empty() && !FoxIsArchiveFile(m_archivePath.c_str()))
            acc = SFGAO_STREAM;
        else
            acc = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_STORAGE | SFGAO_STORAGEANCESTOR;
    }
    for (UINT i = 0; i < cidl; i++)
    {
        const FoxItemID* f = FoxFromItem(apidl[i]);
        SFGAOF a;
        if (f && (f->kind == FOX_FOLDER || f->kind == FOX_ARCHIVE))
            a = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_STORAGE | SFGAO_STORAGEANCESTOR | SFGAO_CANCOPY; // STORAGE/ANCESTOR: navigable + searchable container (matches zipfldr)
        else
            a = SFGAO_STREAM | SFGAO_CANCOPY;
        acc &= a;
    }
    *rgf &= acc;
#ifdef FOX_LOG
    {
        const FoxItemID* f0 = cidl ? FoxFromItem(apidl[0]) : nullptr;
        FoxLog("GetAttributesOf cidl=%u name='%ls' kind=%d in=0x%08X out=0x%08X",
               cidl, f0 ? f0->name : L"(self)", f0 ? (int)f0->kind : -1,
               (unsigned)inMask, (unsigned)*rgf);
    }
#endif
    return S_OK;
}

IFACEMETHODIMP FoxShellFolder::GetUIObjectOf(HWND hwnd, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT*, void** ppv)
{
    if (ppv) *ppv = nullptr;
    if (cidl == 0) return E_INVALIDARG;
#ifdef FOX_LOG
    { const FoxItemID* f0 = FoxFromItem(apidl[0]);
      wchar_t g[64]; StringFromGUID2(riid, g, 64);
      FoxLog("GetUIObjectOf name='%ls' iid=%ls", f0 ? f0->name : L"?", g); }
#endif

    // Context menu (and therefore the default double-click "open"/navigate verb)
    // comes from the shell's default menu, backed by THIS IShellFolder.
    if (riid == IID_IContextMenu || riid == IID_IContextMenu2 || riid == IID_IContextMenu3)
    {
        DEFCONTEXTMENU dcm = {};
        dcm.hwnd  = hwnd;
        dcm.psf   = static_cast<IShellFolder*>(this);
        dcm.cidl  = cidl;
        dcm.apidl = apidl;
        IContextMenu* inner = nullptr;
        HRESULT hr = SHCreateDefaultContextMenu(&dcm, IID_PPV_ARGS(&inner));
        if (FAILED(hr)) return hr;

        // For a single item, wrap so our Open verb navigates (folders/archives)
        // or extracts+launches (files). Multi-select uses the default menu.
        const FoxItemID* f = (cidl == 1) ? FoxFromItem(apidl[0]) : nullptr;
        if (f)
        {
            auto* wrap = new (std::nothrow) CFoxContextMenu(this, inner, apidl[0]);
            inner->Release();
            if (!wrap) return E_OUTOFMEMORY;
            hr = wrap->QueryInterface(riid, ppv);
            wrap->Release();
            return hr;
        }
        hr = inner->QueryInterface(riid, ppv);
        inner->Release();
        return hr;
    }

    // Icons: hand the shell a default extractor pointed at the standard folder
    // or file glyphs so items don't render as blank documents.
    if (riid == IID_IExtractIconW || riid == IID_IExtractIconA)
    {
        const FoxItemID* f = FoxFromItem(apidl[0]);
        bool folder = f && (f->kind == FOX_FOLDER || f->kind == FOX_ARCHIVE);
        IDefaultExtractIconInit* init = nullptr;
        HRESULT hr = SHCreateDefaultExtractIcon(IID_PPV_ARGS(&init));
        if (FAILED(hr)) return hr;
        if (folder)
        {
            init->SetNormalIcon(L"shell32.dll", 3);   // closed folder
            init->SetOpenIcon(L"shell32.dll", 4);     // open folder
        }
        else
        {
            init->SetNormalIcon(L"shell32.dll", 0);   // generic file
        }
        hr = init->QueryInterface(riid, ppv);
        init->Release();
        return hr;
    }

    // Copy / drag-out / extract. We hand back our OWN data object (FoxDataObject)
    // that carries the selected files as CFSTR_FILEDESCRIPTOR + CFSTR_FILECONTENTS.
    // The shell's stock data object only advertises a virtual PIDL list, which a
    // filesystem drop target can't turn into files (drop appears to do nothing) —
    // exposing the descriptor/contents pair is what makes the bytes actually land.
    // Folders in the selection are walked recursively. File-open dialogs and
    // BindToObject(IID_IStream) cover the picker/extract paths separately.
    if (riid == IID_IDataObject)
        return CreateFoxDataObject(m_archivePath, m_chain, m_dirPath, cidl, apidl, riid, ppv);

    // Texture thumbnails. Explorer's per-extension IThumbnailProvider only fires
    // for real filesystem files; for a virtual item INSIDE our folder the shell
    // asks the FOLDER — and for NSE items it requests the classic IExtractImage
    // (not IThumbnailProvider). Answer all three, pointed at the item's full,
    // archive-qualified parse path (so it decodes from the .dat/.fpk/.pftxs).
    if ((riid == IID_IThumbnailProvider || riid == IID_IExtractImage ||
         riid == IID_IExtractImage2) && cidl == 1)
    {
        const FoxItemID* f = FoxFromItem(apidl[0]);
        if (f && f->kind == FOX_FILE && f->name)
        {
            size_t len = wcslen(f->name);
            bool isFtex  = len > 5 && _wcsicmp(f->name + len - 5, L".ftex")  == 0;
            bool isFtexs = len > 6 && _wcsicmp(f->name + len - 6, L".ftexs") == 0;
            if (isFtex || isFtexs)
            {
                STRRET sr;
                if (SUCCEEDED(GetDisplayNameOf(apidl[0], SHGDN_FORPARSING, &sr)))
                {
                    wchar_t* full = nullptr;
                    if (SUCCEEDED(StrRetToStrW(&sr, apidl[0], &full)) && full)
                    {
                        HRESULT hr = CreateFoxThumbProviderForPath(full, riid, ppv);
                        CoTaskMemFree(full);
                        return hr;
                    }
                }
            }
        }
        return E_NOINTERFACE;
    }

    return E_NOINTERFACE;
}

IFACEMETHODIMP FoxShellFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF flags, STRRET* name)
{
    const FoxItemID* f = FoxFromItem(pidl);
    if (!f || !name) return E_INVALIDARG;
    name->uType = STRRET_WSTR;

    // SHGDN_FORPARSING WITHOUT SHGDN_INFOLDER must return a fully-qualified,
    // shell-parseable path (archive\interior\leaf), exactly like zipfldr does
    // ("C:\..\foo.zip\Assets"). The shell uses this name to identify and
    // NAVIGATE into the item on double-click; returning just the leaf silently
    // breaks DefView sub-folder navigation (bind works, but the view never
    // follows). Everything else returns the leaf name.
    if ((flags & SHGDN_FORPARSING) && !(flags & SHGDN_INFOLDER))
    {
        std::wstring full = m_archivePath;
        auto append = [&](const std::wstring& s) {
            if (s.empty()) return;
            std::wstring t = s;
            for (auto& ch : t) if (ch == L'/') ch = L'\\';
            full += L"\\"; full += t;
        };
        for (const auto& seg : m_chain) append(seg);
        append(m_dirPath);
        full += L"\\"; full += f->name;
        return SHStrDupW(full.c_str(), &name->pOleStr);
    }
    return SHStrDupW(f->name, &name->pOleStr);
}

IFACEMETHODIMP FoxShellFolder::SetNameOf(HWND, PCUITEMID_CHILD, LPCWSTR, SHGDNF, PITEMID_CHILD*)
{ return E_NOTIMPL; } // read-only

IFACEMETHODIMP FoxShellFolder::ParseDisplayName(HWND, IBindCtx*, LPWSTR pszName, ULONG*,
                                                PIDLIST_RELATIVE* ppidl, ULONG*)
{
    if (!pszName || !ppidl) return E_INVALIDARG;
    *ppidl = nullptr;

    // First path segment (accept both separators).
    std::wstring rest = pszName;
    while (!rest.empty() && (rest[0] == L'\\' || rest[0] == L'/')) rest.erase(0, 1);
    size_t sep = rest.find_first_of(L"\\/");
    std::wstring seg = (sep == std::wstring::npos) ? rest : rest.substr(0, sep);
    std::wstring tail = (sep == std::wstring::npos) ? L"" : rest.substr(sep + 1);
    if (seg.empty()) return E_INVALIDARG;

    // Find the matching child by listing this directory.
    auto& br = Bridge::Get();
    bool owns = false;
    FoxArchive* h = br.OpenChain(m_archivePath, m_chain, owns);
    if (!h) return E_FAIL;
    FoxList* list = nullptr;
    if (br.list(h, m_dirPath.c_str(), &list) != FOXARC_OK) { br.ReleaseChain(h, owns); return E_FAIL; }

    int n = 0; br.list_count(list, &n);
    LPITEMIDLIST childRel = nullptr;
    for (int i = 0; i < n; i++)
    {
        FoxItemInfo info{};
        if (br.list_item(list, i, &info) != FOXARC_OK) continue;
        if (CompareStringOrdinal(info.name, -1, seg.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            FoxKind k = info.isArchive ? FOX_ARCHIVE : info.isFolder ? FOX_FOLDER : FOX_FILE;
            childRel = FoxCreateItem(k, info.name, info.size, info.pathHash);
            break;
        }
    }
    br.list_free(list);
    br.ReleaseChain(h, owns);
    if (!childRel) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    if (tail.empty()) { *ppidl = childRel; return S_OK; }

    // Recurse into the child folder for the remainder.
    IShellFolder* sub = nullptr;
    HRESULT hr = BindToObject(childRel, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&sub));
    if (FAILED(hr)) { ILFree(childRel); return hr; }
    PIDLIST_RELATIVE subPidl = nullptr;
    hr = sub->ParseDisplayName(nullptr, nullptr, const_cast<LPWSTR>(tail.c_str()), nullptr, &subPidl, nullptr);
    sub->Release();
    if (FAILED(hr)) { ILFree(childRel); return hr; }

    *ppidl = ILCombine(reinterpret_cast<PCIDLIST_ABSOLUTE>(childRel), subPidl);
    ILFree(childRel); ILFree(subPidl);
    return *ppidl ? S_OK : E_OUTOFMEMORY;
}

// IShellFolder2
IFACEMETHODIMP FoxShellFolder::GetDefaultSearchGUID(GUID*) { return E_NOTIMPL; }
IFACEMETHODIMP FoxShellFolder::EnumSearches(IEnumExtraSearch** p) { if (p) *p = nullptr; return E_NOTIMPL; }
IFACEMETHODIMP FoxShellFolder::GetDefaultColumn(DWORD, ULONG* pSort, ULONG* pDisplay)
{ if (pSort) *pSort = 0; if (pDisplay) *pDisplay = 0; return S_OK; }

// Property keys (defined explicitly to avoid initguid/propkey.h include-order
// pitfalls where the PKEY_* constants can resolve to all-zero).
static const PROPERTYKEY kPKEY_ItemNameDisplay = { { 0xB725F130, 0x47EF, 0x101A, { 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC } }, 10 };
static const PROPERTYKEY kPKEY_Size            = { { 0xB725F130, 0x47EF, 0x101A, { 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC } }, 12 };
static const PROPERTYKEY kPKEY_ItemType        = { { 0x28636AA6, 0x953D, 0x11D2, { 0xB5, 0xD6, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } }, 11 };
static const PROPERTYKEY kPKEY_ItemTypeText    = { { 0x28636AA6, 0x953D, 0x11D2, { 0xB5, 0xD6, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0 } }, 4 };
// System.FileName / System.ItemNameDisplayWithoutExtension — the keys Explorer's
// search box builds its filename condition on. Answering them is what lets a
// search inside an archive actually match items by name.
static const PROPERTYKEY kPKEY_FileName        = { { 0x41CF5AE0, 0xF75A, 0x4806, { 0xBD, 0x87, 0x59, 0xC7, 0xD9, 0x24, 0x8E, 0xB9 } }, 100 };
static const PROPERTYKEY kPKEY_NameNoExt       = { { 0xB725F130, 0x47EF, 0x101A, { 0xA5, 0xF1, 0x02, 0x60, 0x8C, 0x9E, 0xEB, 0xAC } }, 8 };

static inline bool PkEq(const PROPERTYKEY& a, const PROPERTYKEY& b)
{ return a.pid == b.pid && IsEqualGUID(a.fmtid, b.fmtid); }

// Columns: 0 = Name, 1 = Size, 2 = Type (friendly, from foxtypes.h).
static const UINT FOX_COL_COUNT = 3;

// Friendly "Type" text for an item (e.g. .fpk -> "Fox Package", folder ->
// "File folder"). Archives are file-kind for typing (so .fpk reads "Fox
// Package", not "File folder") even though they browse like folders.
static void FoxItemTypeText(const FoxItemID* f, wchar_t* out, size_t cap)
{
    FoxFriendlyType(f->name, f->kind == FOX_FOLDER, f->kind == FOX_ARCHIVE, out, cap);
}

IFACEMETHODIMP FoxShellFolder::GetDefaultColumnState(UINT col, SHCOLSTATEF* p)
{
    if (!p) return E_POINTER;
    switch (col)
    {
    case 0: *p = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT; return S_OK;
    case 1: *p = SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT; return S_OK;
    case 2: *p = SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT; return S_OK;
    default: return E_INVALIDARG;
    }
}

// Explorer's built-in "Type" column reads System.ItemTypeText via this. Filling
// it is what replaces the single registered ProgID name ("MGSV Fox Archive")
// with the real per-item type.
IFACEMETHODIMP FoxShellFolder::GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid, VARIANT* pv)
{
    if (!pidl || !pscid || !pv) return E_INVALIDARG;
    const FoxItemID* f = FoxFromItem(pidl);
    if (!f) return E_INVALIDARG;
    VariantInit(pv);

    if (PkEq(*pscid, kPKEY_ItemTypeText))
    {
        wchar_t type[160]; FoxItemTypeText(f, type, 160);
        pv->vt = VT_BSTR; pv->bstrVal = SysAllocString(type);
        return pv->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (PkEq(*pscid, kPKEY_ItemType))   // the extension, e.g. ".fpk"
    {
        const wchar_t* dot = wcsrchr(f->name, L'.');
        pv->vt = VT_BSTR; pv->bstrVal = SysAllocString(dot ? dot : L"");
        return pv->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (PkEq(*pscid, kPKEY_ItemNameDisplay) || PkEq(*pscid, kPKEY_FileName))
    {
        pv->vt = VT_BSTR; pv->bstrVal = SysAllocString(f->name);
        return pv->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (PkEq(*pscid, kPKEY_NameNoExt))   // name without the trailing extension
    {
        std::wstring stem = f->name;
        size_t dot = stem.find_last_of(L'.');
        if (dot != std::wstring::npos) stem.resize(dot);
        pv->vt = VT_BSTR; pv->bstrVal = SysAllocString(stem.c_str());
        return pv->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (PkEq(*pscid, kPKEY_Size) && f->kind == FOX_FILE)
    {
        pv->vt = VT_UI8; pv->ullVal = f->size;
        return S_OK;
    }
    return E_NOTIMPL;
}

IFACEMETHODIMP FoxShellFolder::GetDetailsOf(PCUITEMID_CHILD pidl, UINT col, SHELLDETAILS* psd)
{
    if (!psd) return E_POINTER;
    if (col >= FOX_COL_COUNT) return E_INVALIDARG;

    if (!pidl) // column header
    {
        const wchar_t* title = (col == 0) ? L"Name" : (col == 1) ? L"Size" : L"Type";
        psd->fmt = (col == 1) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        psd->cxChar = (col == 0) ? 30 : (col == 1) ? 12 : 20;
        psd->str.uType = STRRET_WSTR;
        return SHStrDupW(title, &psd->str.pOleStr);
    }

    const FoxItemID* f = FoxFromItem(pidl);
    if (!f) return E_INVALIDARG;
    psd->str.uType = STRRET_WSTR;

    if (col == 0)
        return SHStrDupW(f->name, &psd->str.pOleStr);

    if (col == 2)
    {
        wchar_t type[160]; FoxItemTypeText(f, type, 160);
        return SHStrDupW(type, &psd->str.pOleStr);
    }

    wchar_t buf[32];
    if (f->kind == FOX_FILE) StrFormatByteSizeW((LONGLONG)f->size, buf, 32);
    else wcscpy_s(buf, L"");
    return SHStrDupW(buf, &psd->str.pOleStr);
}

IFACEMETHODIMP FoxShellFolder::MapColumnToSCID(UINT col, SHCOLUMNID* scid)
{
    if (!scid) return E_POINTER;
    switch (col)
    {
    case 0: *scid = kPKEY_ItemNameDisplay; return S_OK;
    case 1: *scid = kPKEY_Size;            return S_OK;
    case 2: *scid = kPKEY_ItemTypeText;    return S_OK;
    default: return E_FAIL;
    }
}
