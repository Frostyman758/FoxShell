#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include "FoxContextMenu.h"
#include "FoxShellFolder.h"
#include "dllref.h"
#include "foxlog.h"

#pragma comment(lib, "shlwapi.lib")

CFoxContextMenu::CFoxContextMenu(FoxShellFolder* folder, IContextMenu* inner, PCUITEMID_CHILD child)
    : m_folder(folder), m_inner(inner)
{
    m_folder->AddRef();
    m_inner->AddRef();
    m_child = reinterpret_cast<PITEMID_CHILD>(ILClone(child));
    if (const FoxItemID* fi = FoxFromItem(reinterpret_cast<LPCITEMIDLIST>(m_child)))
    { m_kind = static_cast<FoxKind>(fi->kind); m_name = fi->name; }
    DllAddRef();
}

CFoxContextMenu::~CFoxContextMenu()
{
    if (m_child) ILFree(m_child);
    if (m_site) m_site->Release();
    m_inner->Release();
    m_folder->Release();
    DllRelease();
}

// IUnknown
IFACEMETHODIMP CFoxContextMenu::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IContextMenu ||
        riid == IID_IContextMenu2 || riid == IID_IContextMenu3)
        *ppv = static_cast<IContextMenu3*>(this);
    else if (riid == IID_IObjectWithSite)
        *ppv = static_cast<IObjectWithSite*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }
    AddRef(); return S_OK;
}
IFACEMETHODIMP_(ULONG) CFoxContextMenu::AddRef() { return InterlockedIncrement(&m_ref); }
IFACEMETHODIMP_(ULONG) CFoxContextMenu::Release()
{ LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

// IContextMenu
IFACEMETHODIMP CFoxContextMenu::QueryContextMenu(HMENU h, UINT indexMenu, UINT idFirst, UINT idLast, UINT flags)
{
    HRESULT hr = m_inner->QueryContextMenu(h, indexMenu, idFirst, idLast, flags);
    UINT added = SUCCEEDED(hr) ? (UINT)HRESULT_CODE(hr) : 0;

    // The shell's default menu for a VIRTUAL file is nearly empty (just Copy).
    // Inject our own "Open" at the top and make it the default, so double-click
    // and right-click->Open both extract+launch the file.
    if (m_kind == FOX_FILE && (idFirst + added) < idLast)
    {
        m_openOffset = added;
        InsertMenuW(h, indexMenu, MF_BYPOSITION | MF_STRING, idFirst + added, L"&Open");
        if (!(flags & CMF_DEFAULTONLY))
            InsertMenuW(h, indexMenu + 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        SetMenuDefaultItem(h, indexMenu, TRUE);  // double-click invokes our Open
        added += 1;
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, added);
    }
    return hr;
}

IFACEMETHODIMP CFoxContextMenu::InvokeCommand(CMINVOKECOMMANDINFO* ici)
{
    // Our injected file "Open" (matched by command offset).
    if (m_kind == FOX_FILE && IS_INTRESOURCE(ici->lpVerb) &&
        LOWORD((UINT_PTR)ici->lpVerb) == m_openOffset)
    {
        if (SUCCEEDED(m_folder->OpenChildFile(m_name.c_str(), ici->hwnd))) return S_OK;
    }

    char verbA[64] = "";
    if (!IS_INTRESOURCE(ici->lpVerb))
        lstrcpynA(verbA, ici->lpVerb, 64);
    else
        m_inner->GetCommandString(LOWORD(ici->lpVerb), GCS_VERBA, nullptr, verbA, 64);
    FoxLog("ContextMenu Invoke verb='%hs' kind=%d", verbA, (int)m_kind);

    bool isOpen = (_stricmp(verbA, "open") == 0);

    if (isOpen && m_kind == FOX_FILE)
    {
        if (SUCCEEDED(m_folder->OpenChildFile(m_name.c_str(), ici->hwnd))) return S_OK;
    }
    else if (m_kind == FOX_FOLDER || m_kind == FOX_ARCHIVE)
    {
        UINT sbsp = 0;
        if      (isOpen)                                sbsp = SBSP_SAMEBROWSER | SBSP_DEFMODE;
        else if (_stricmp(verbA, "opennewwindow") == 0) sbsp = SBSP_NEWBROWSER  | SBSP_DEFMODE;
        else if (_stricmp(verbA, "opennewtab") == 0)    sbsp = SBSP_NEWBROWSER  | SBSP_DEFMODE;
        if (sbsp && Navigate(sbsp)) return S_OK;
    }

    return m_inner->InvokeCommand(ici);
}

IFACEMETHODIMP CFoxContextMenu::GetCommandString(UINT_PTR id, UINT type, UINT* r, CHAR* buf, UINT cch)
{ return m_inner->GetCommandString(id, type, r, buf, cch); }

// IContextMenu2/3
IFACEMETHODIMP CFoxContextMenu::HandleMenuMsg(UINT msg, WPARAM w, LPARAM l)
{ IContextMenu2* p; if (SUCCEEDED(m_inner->QueryInterface(IID_PPV_ARGS(&p)))) { HRESULT hr = p->HandleMenuMsg(msg, w, l); p->Release(); return hr; } return E_NOTIMPL; }
IFACEMETHODIMP CFoxContextMenu::HandleMenuMsg2(UINT msg, WPARAM w, LPARAM l, LRESULT* res)
{ IContextMenu3* p; if (SUCCEEDED(m_inner->QueryInterface(IID_PPV_ARGS(&p)))) { HRESULT hr = p->HandleMenuMsg2(msg, w, l, res); p->Release(); return hr; } return E_NOTIMPL; }

// IObjectWithSite
IFACEMETHODIMP CFoxContextMenu::SetSite(IUnknown* site)
{
    if (m_site) m_site->Release();
    m_site = site;
    if (m_site) m_site->AddRef();
    IUnknown_SetSite(m_inner, site); // let the default menu have it too
    return S_OK;
}
IFACEMETHODIMP CFoxContextMenu::GetSite(REFIID riid, void** ppv)
{ return m_site ? m_site->QueryInterface(riid, ppv) : E_FAIL; }

bool CFoxContextMenu::Navigate(UINT sbsp)
{
    if (!m_site || !m_child) return false;
    IShellBrowser* sb = nullptr;
    IUnknown_QueryService(m_site, SID_SShellBrowser, IID_IShellBrowser, reinterpret_cast<void**>(&sb));
    if (!sb) return false;
    HRESULT hr = sb->BrowseObject(reinterpret_cast<PCUIDLIST_RELATIVE>(m_child), sbsp | SBSP_RELATIVE);
    FoxLog("ContextMenu BrowseObject hr=0x%08X", hr);
    sb->Release();
    return SUCCEEDED(hr);
}
