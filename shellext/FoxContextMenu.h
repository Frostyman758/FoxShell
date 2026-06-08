#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>
#include "pidl.h"

class FoxShellFolder;

// Context-menu wrapper for our items. The shell's DEFAULT folder menu provides
// the verbs and look, but its "open" does not reliably navigate when the items
// live in a third-party namespace extension (DefView binds the folder but the
// view never follows). So we intercept the navigation verbs and drive the
// browser ourselves via the site's IShellBrowser::BrowseObject — the same thing
// the .dat's file-association "open" verb effectively does. For files we inject
// our own "Open" that extracts + launches. Everything else is delegated to the
// inner default menu.
class CFoxContextMenu : public IContextMenu3, public IObjectWithSite
{
public:
    CFoxContextMenu(FoxShellFolder* folder, IContextMenu* inner, PCUITEMID_CHILD child);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(HMENU h, UINT indexMenu, UINT idFirst, UINT idLast, UINT flags) override;
    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* ici) override;
    IFACEMETHODIMP GetCommandString(UINT_PTR id, UINT type, UINT* r, CHAR* buf, UINT cch) override;

    // IContextMenu2/3 — needed so owner-drawn/submenu items render.
    IFACEMETHODIMP HandleMenuMsg(UINT msg, WPARAM w, LPARAM l) override;
    IFACEMETHODIMP HandleMenuMsg2(UINT msg, WPARAM w, LPARAM l, LRESULT* res) override;

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* site) override;
    IFACEMETHODIMP GetSite(REFIID riid, void** ppv) override;

private:
    ~CFoxContextMenu();
    bool Navigate(UINT sbsp);

    LONG m_ref = 1;
    FoxShellFolder* m_folder;
    IContextMenu* m_inner;
    PITEMID_CHILD m_child;
    IUnknown* m_site = nullptr;
    FoxKind m_kind = FOX_FILE;
    std::wstring m_name;
    UINT m_openOffset = 0xFFFF; // command offset of our injected "Open" (files)
};
