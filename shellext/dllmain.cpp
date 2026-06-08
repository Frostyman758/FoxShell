#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <olectl.h>
#include <new>
#include "guid.h"
#include "bridge.h"
#include "FoxShellFolder.h"
#include "dllref.h"
#include "foxlog.h"

static HINSTANCE g_hinst = nullptr;
static LONG      g_cRefModule = 0;

void DllAddRef()  { InterlockedIncrement(&g_cRefModule); }
void DllRelease() { InterlockedDecrement(&g_cRefModule); }

// Created in FoxThumbnail.cpp.
HRESULT CreateFoxThumbProvider(REFIID riid, void** ppv);

// ── Class factory ────────────────────────────────────────────────────────────
class FoxClassFactory : public IClassFactory
{
public:
    explicit FoxClassFactory(const CLSID& clsid) : m_clsid(clsid) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        { *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (ppv) *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        if (m_clsid == CLSID_FoxThumbProvider)
            return CreateFoxThumbProvider(riid, ppv);
        auto* f = new (std::nothrow) FoxShellFolder();
        if (!f) return E_OUTOFMEMORY;
        HRESULT hr = f->QueryInterface(riid, ppv);
        f->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override
    { if (lock) DllAddRef(); else DllRelease(); return S_OK; }

private:
    CLSID m_clsid;
    LONG m_ref = 1;
};

// ── DLL exports ──────────────────────────────────────────────────────────────
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (ppv) *ppv = nullptr;
    FoxLog("DllGetClassObject");
    if (rclsid != CLSID_FoxShellFolder && rclsid != CLSID_FoxThumbProvider)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* cf = new (std::nothrow) FoxClassFactory(rclsid);
    if (!cf) return E_OUTOFMEMORY;
    HRESULT hr = cf->QueryInterface(riid, ppv);
    cf->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_cRefModule == 0 ? S_OK : S_FALSE; }

// Registry helpers (HKCU\Software\Classes — per-user, no elevation needed).
static LONG SetVal(HKEY root, const wchar_t* subkey, const wchar_t* name,
                   const wchar_t* data)
{
    HKEY k;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(data),
                        (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return rc;
}
static LONG SetDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD v)
{
    HKEY k;
    LONG rc = RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    rc = RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(k);
    return rc;
}

// Junction-root attributes. Matches zipfldr's CompressedFolder (proven to make
// the shell mount the file as a folder): SFGAO_FOLDER | drop/capability bits.
static const DWORD FOX_SHELLFOLDER_ATTRS = 0x200001A0;

STDAPI DllRegisterServer()
{
    wchar_t module[MAX_PATH];
    GetModuleFileNameW(g_hinst, module, MAX_PATH);

    const HKEY R = HKEY_CURRENT_USER;
    const wchar_t* C = L"Software\\Classes\\CLSID\\" FOX_CLSID_STR;
    wchar_t sub[512];

    if (SetVal(R, C, nullptr, FOX_FRIENDLY) != ERROR_SUCCESS) return SELFREG_E_CLASS;

    swprintf(sub, 512, L"%s\\InprocServer32", C);
    SetVal(R, sub, nullptr, module);
    SetVal(R, sub, L"ThreadingModel", L"Apartment");

    swprintf(sub, 512, L"%s\\ShellFolder", C);
    SetDword(R, sub, L"Attributes", FOX_SHELLFOLDER_ATTRS);

    // CATID_BrowsableShellExt — THIS is what makes the shell treat a file of
    // our type as a browsable folder junction (verified: without it the shell
    // reports SFGAO_STREAM only and refuses to mount). Same category .zip uses.
    swprintf(sub, 512, L"%s\\Implemented Categories\\{00021490-0000-0000-C000-000000000046}", C);
    SetVal(R, sub, nullptr, L"");

    // ProgID back-reference (mirrors zipfldr's CompressedFolder registration).
    swprintf(sub, 512, L"%s\\ProgID", C);
    SetVal(R, sub, nullptr, L"Fox.Archive");

    swprintf(sub, 512, L"%s\\DefaultIcon", C);
    wchar_t icon[MAX_PATH + 8]; swprintf(icon, MAX_PATH + 8, L"%s,0", module);
    SetVal(R, sub, nullptr, icon);

    // ── FTEX thumbnail provider ──────────────────────────────────────────────
    const wchar_t* TC = L"Software\\Classes\\CLSID\\" FOX_THUMB_CLSID_STR;
    SetVal(R, TC, nullptr, L"MGSV Fox Texture Thumbnail");
    swprintf(sub, 512, L"%s\\InprocServer32", TC);
    SetVal(R, sub, nullptr, module);
    SetVal(R, sub, L"ThreadingModel", L"Apartment");
    // Run the handler IN-PROCESS, not in the isolated thumbnail surrogate
    // (prevhost.exe). Our NativeAOT bridge loads + decodes fine in explorer.exe
    // (the namespace-extension path proves it) but the surrogate returns blank.
    SetDword(R, TC, L"DisableProcessIsolation", 1);

    // Register the IThumbnailProvider catid for .ftex AND .ftexs, under BOTH the
    // extension key and SystemFileAssociations. The latter is the robust spot
    // for a thumbnail handler on a type with no default program (which .ftex is)
    // — some shells only honour the SystemFileAssociations entry there.
    const wchar_t* TCATID = L"shellex\\{E357FCCD-A995-4576-B01F-234630154E96}";
    swprintf(sub, 512, L"Software\\Classes\\.ftex\\%s", TCATID);
    SetVal(R, sub, nullptr, FOX_THUMB_CLSID_STR);
    swprintf(sub, 512, L"Software\\Classes\\.ftexs\\%s", TCATID);
    SetVal(R, sub, nullptr, FOX_THUMB_CLSID_STR);
    swprintf(sub, 512, L"Software\\Classes\\SystemFileAssociations\\.ftex\\%s", TCATID);
    SetVal(R, sub, nullptr, FOX_THUMB_CLSID_STR);
    swprintf(sub, 512, L"Software\\Classes\\SystemFileAssociations\\.ftexs\\%s", TCATID);
    SetVal(R, sub, nullptr, FOX_THUMB_CLSID_STR);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" FOX_CLSID_STR);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" FOX_THUMB_CLSID_STR);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.ftex\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\.ftexs\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SystemFileAssociations\\.ftex\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SystemFileAssociations\\.ftexs\\shellex\\{E357FCCD-A995-4576-B01F-234630154E96}");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hinst = h;
        DisableThreadLibraryCalls(h);
        Bridge::Get().SetSelf(h);
    }
    return TRUE;
}
