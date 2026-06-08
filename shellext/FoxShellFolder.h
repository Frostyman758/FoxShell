#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

// One IShellFolder instance = one interior directory within one archive level.
//
// State (carried in the C++ object, propagated through BindToObject):
//   m_archivePath : the top-level .dat/.fpk on disk
//   m_chain       : interior paths of nested archives, top -> innermost
//                   (empty => we're inside the top-level archive)
//   m_dirPath     : interior directory within the innermost archive ("" = root)
//   m_pidlAbs     : absolute PIDL of this folder (for GetCurFolder / children)
class FoxShellFolder : public IShellFolder2, public IPersistFolder3
{
public:
    FoxShellFolder();
    ~FoxShellFolder();

    // Construct a child folder from this one + a single item PIDL.
    HRESULT InitAsChild(FoxShellFolder* parent, const struct FoxItemID* item);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IPersist / IPersistFolder / IPersistFolder2 / IPersistFolder3
    IFACEMETHODIMP GetClassID(CLSID* pClassID) override;
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl) override;
    IFACEMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE* ppidl) override;
    IFACEMETHODIMP InitializeEx(IBindCtx* pbc, PCIDLIST_ABSOLUTE pidlRoot,
                                const PERSIST_FOLDER_TARGET_INFO* ppfti) override;
    IFACEMETHODIMP GetFolderTargetInfo(PERSIST_FOLDER_TARGET_INFO* ppfti) override;

    // IShellFolder
    IFACEMETHODIMP ParseDisplayName(HWND, IBindCtx*, LPWSTR, ULONG*, PIDLIST_RELATIVE*, ULONG*) override;
    IFACEMETHODIMP EnumObjects(HWND, SHCONTF, IEnumIDList**) override;
    IFACEMETHODIMP BindToObject(PCUIDLIST_RELATIVE, IBindCtx*, REFIID, void**) override;
    IFACEMETHODIMP BindToStorage(PCUIDLIST_RELATIVE, IBindCtx*, REFIID, void**) override;
    IFACEMETHODIMP CompareIDs(LPARAM, PCUIDLIST_RELATIVE, PCUIDLIST_RELATIVE) override;
    IFACEMETHODIMP CreateViewObject(HWND, REFIID, void**) override;
    IFACEMETHODIMP GetAttributesOf(UINT, PCUITEMID_CHILD_ARRAY, SFGAOF*) override;
    IFACEMETHODIMP GetUIObjectOf(HWND, UINT, PCUITEMID_CHILD_ARRAY, REFIID, UINT*, void**) override;
    IFACEMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD, SHGDNF, STRRET*) override;
    IFACEMETHODIMP SetNameOf(HWND, PCUITEMID_CHILD, LPCWSTR, SHGDNF, PITEMID_CHILD*) override;

    // IShellFolder2
    IFACEMETHODIMP GetDefaultSearchGUID(GUID*) override;
    IFACEMETHODIMP EnumSearches(IEnumExtraSearch**) override;
    IFACEMETHODIMP GetDefaultColumn(DWORD, ULONG*, ULONG*) override;
    IFACEMETHODIMP GetDefaultColumnState(UINT, SHCOLSTATEF*) override;
    IFACEMETHODIMP GetDetailsEx(PCUITEMID_CHILD, const SHCOLUMNID*, VARIANT*) override;
    IFACEMETHODIMP GetDetailsOf(PCUITEMID_CHILD, UINT, SHELLDETAILS*) override;
    IFACEMETHODIMP MapColumnToSCID(UINT, SHCOLUMNID*) override;

    // Accessors used by the enumerator / UI objects.
    const std::wstring& ArchivePath() const { return m_archivePath; }
    const std::vector<std::wstring>& Chain() const { return m_chain; }
    const std::wstring& DirPath() const { return m_dirPath; }

    // Extract a child file to a managed temp folder and open it with its
    // associated app (e.g. .lua -> the user's editor). Only invoked when the
    // user opens a file; browsing extracts nothing.
    HRESULT OpenChildFile(const wchar_t* leafName, HWND hwnd);

private:
    // The interior path within the innermost archive for a child item.
    std::wstring ChildInteriorPath(const wchar_t* name) const;

    // Keep the bridge's per-archive refcount in step with m_archivePath, so the
    // cached archive index is evicted (and its memory trimmed) once no folder
    // is browsing it any more.
    void SyncArchiveAcquire();

    LONG m_ref = 1;
    std::wstring m_archivePath;
    std::wstring m_acqArchive;     // the path currently acquired from Bridge
    std::vector<std::wstring> m_chain;
    std::wstring m_dirPath;
    PIDLIST_ABSOLUTE m_pidlAbs = nullptr;
};
