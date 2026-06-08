// Isolated test of the namespace extension's COM logic. Loads foxshellext.dll,
// creates the folder via its class factory (NO registry, NO explorer), points
// it at a real archive, and walks it like the shell would: EnumObjects,
// GetDisplayNameOf, GetAttributesOf, BindToObject, nested drill-in.
//
// A crash here takes down only this process — never explorer.exe.
//
// Usage: nse_test.exe <foxshellext.dll> <archivePath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

typedef HRESULT (__stdcall *pfnDllGetClassObject)(REFCLSID, REFIID, void**);
// {5AA92D71-4013-463E-BFDE-673DB7C70FCF}
static const CLSID CLSID_Fox =
{ 0x5aa92d71, 0x4013, 0x463e, { 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf } };

static void NameOf(IShellFolder* sf, PCUITEMID_CHILD c, wchar_t* out, size_t cap)
{
    STRRET sr;
    if (SUCCEEDED(sf->GetDisplayNameOf(c, SHGDN_NORMAL, &sr)))
        StrRetToBufW(&sr, c, out, (UINT)cap);
    else wcscpy_s(out, cap, L"?");
}

static int Walk(IShellFolder* sf, int depth, int maxDepth)
{
    IEnumIDList* e = nullptr;
    if (FAILED(sf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e)) || !e)
    { wprintf(L"%*sEnumObjects failed\n", depth * 2, L""); return 0; }

    int count = 0;
    PITEMID_CHILD child = nullptr; ULONG fetched = 0;
    while (e->Next(1, &child, &fetched) == S_OK && fetched == 1)
    {
        count++;
        wchar_t nm[260]; NameOf(sf, child, nm, 260);
        SFGAOF attr = SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_STREAM;
        sf->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attr);
        const wchar_t* tag = (attr & SFGAO_FOLDER) ? L"[D]" : L"   ";
        wprintf(L"%*s%s %s\n", depth * 2, L"", tag, nm);

        if ((attr & SFGAO_FOLDER) && depth < maxDepth)
        {
            IShellFolder* sub = nullptr;
            if (SUCCEEDED(sf->BindToObject(child, nullptr, IID_IShellFolder, (void**)&sub)) && sub)
            {
                Walk(sub, depth + 1, maxDepth);
                sub->Release();
            }
        }
        CoTaskMemFree(child);
        if (count >= 200) { wprintf(L"%*s...(stop at 200)\n", depth*2, L""); break; }
    }
    e->Release();
    return count;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: nse_test <foxshellext.dll> <archive>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) { wprintf(L"LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto getCO = (pfnDllGetClassObject)GetProcAddress(m, "DllGetClassObject");
    if (!getCO) { wprintf(L"no DllGetClassObject\n"); return 1; }

    IClassFactory* cf = nullptr;
    HRESULT hr = getCO(CLSID_Fox, IID_IClassFactory, (void**)&cf);
    if (FAILED(hr) || !cf) { wprintf(L"DllGetClassObject hr=0x%08X\n", hr); return 1; }

    IPersistFolder* pf = nullptr;
    hr = cf->CreateInstance(nullptr, IID_IPersistFolder, (void**)&pf);
    cf->Release();
    if (FAILED(hr) || !pf) { wprintf(L"CreateInstance hr=0x%08X\n", hr); return 1; }

    PIDLIST_ABSOLUTE pidl = nullptr;
    hr = SHParseDisplayName(argv[2], nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl) { wprintf(L"SHParseDisplayName(%s) hr=0x%08X\n", argv[2], hr); return 1; }

    hr = pf->Initialize(pidl);
    wprintf(L"Initialize hr=0x%08X\n", hr);

    IShellFolder* sf = nullptr;
    hr = pf->QueryInterface(IID_IShellFolder, (void**)&sf);
    if (FAILED(hr) || !sf) { wprintf(L"QI IShellFolder hr=0x%08X\n", hr); return 1; }

    wprintf(L"=== %s ===\n", argv[2]);
    // Optional argv[3]: an interior path to navigate to first (tests
    // ParseDisplayName + archive BindToObject for nested drill-in).
    if (argc >= 4)
    {
        PIDLIST_RELATIVE rel = nullptr;
        hr = sf->ParseDisplayName(nullptr, nullptr, argv[3], nullptr, &rel, nullptr);
        wprintf(L"ParseDisplayName(%s) hr=0x%08X\n", argv[3], hr);
        if (SUCCEEDED(hr) && rel)
        {
            IShellFolder* target = nullptr;
            hr = sf->BindToObject(rel, nullptr, IID_IShellFolder, (void**)&target);
            wprintf(L"BindToObject hr=0x%08X\n", hr);
            if (SUCCEEDED(hr) && target) { Walk(target, 0, 3); target->Release(); }
            CoTaskMemFree(rel);
        }
    }
    else
    {
        int n = Walk(sf, 0, 6);
        wprintf(L"total top-level walk visited: %d\n", n);
    }

    sf->Release();
    pf->Release();
    CoTaskMemFree(pidl);
    CoUninitialize();
    FreeLibrary(m);
    return 0;
}
