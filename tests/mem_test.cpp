// Memory-release test: drive foxshellext.dll the way Explorer does — create the
// folder, Initialize() on a .dat, browse (EnumObjects + drill into nested
// archives + pull file streams to grow the heap), then RELEASE the folder and
// confirm the working set drops (the folder dtor must evict the cached archive
// and trim the GC heap, not wait for the host to exit).
//
// Usage: mem_test.exe <foxshellext.dll> <archivePath>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <psapi.h>
#include <cstdio>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

static const CLSID CLSID_Fox =
{ 0x5aa92d71, 0x4013, 0x463e, { 0xbf, 0xde, 0x67, 0x3d, 0xb7, 0xc7, 0x0f, 0xcf } };
typedef HRESULT (__stdcall *pfnDllGetClassObject)(REFCLSID, REFIID, void**);

static int WSmb()
{
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return (int)(pmc.WorkingSetSize / (1024 * 1024));
}

// Recursively browse: enumerate, pull a couple of file streams per folder (to
// grow the heap like reads/thumbnails do), and drill into sub-folders/archives.
static void Browse(IShellFolder* sf, int depth, int maxDepth)
{
    IEnumIDList* e = nullptr;
    if (FAILED(sf->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &e)) || !e) return;

    int files = 0, dirs = 0;
    PITEMID_CHILD child = nullptr; ULONG got = 0;
    while (e->Next(1, &child, &got) == S_OK && got == 1)
    {
        SFGAOF attr = SFGAO_FOLDER;
        sf->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attr);
        if (attr & SFGAO_FOLDER)
        {
            if (depth < maxDepth && dirs < 3)
            {
                IShellFolder* sub = nullptr;
                if (SUCCEEDED(sf->BindToObject(child, nullptr, IID_IShellFolder, (void**)&sub)) && sub)
                { Browse(sub, depth + 1, maxDepth); sub->Release(); }
                dirs++;
            }
        }
        else if (files < 4)
        {
            IStream* s = nullptr;     // pull the bytes (same path thumbnails/reads use)
            if (SUCCEEDED(sf->BindToObject(child, nullptr, IID_IStream, (void**)&s)) && s) s->Release();
            files++;
        }
        CoTaskMemFree(child);
    }
    e->Release();
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: mem_test <foxshellext.dll> <archive>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) { wprintf(L"LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto getCO = (pfnDllGetClassObject)GetProcAddress(m, "DllGetClassObject");
    IClassFactory* cf = nullptr;
    if (FAILED(getCO(CLSID_Fox, IID_IClassFactory, (void**)&cf)) || !cf) { wprintf(L"no factory\n"); return 1; }

    wprintf(L"baseline (dll loaded):   %4d MB\n", WSmb());

    IPersistFolder* pf = nullptr;
    cf->CreateInstance(nullptr, IID_IPersistFolder, (void**)&pf);
    cf->Release();
    PIDLIST_ABSOLUTE pidl = nullptr;
    SHParseDisplayName(argv[2], nullptr, &pidl, 0, nullptr);
    pf->Initialize(pidl);
    CoTaskMemFree(pidl);

    IShellFolder* sf = nullptr;
    pf->QueryInterface(IID_IShellFolder, (void**)&sf);
    Browse(sf, 0, 5);
    wprintf(L"after browsing archive:  %4d MB  (peak: cached index + read heap)\n", WSmb());

    sf->Release();
    pf->Release();   // last folder gone -> dtor -> ReleaseArchive -> evict + trim
    wprintf(L"after releasing folder:  %4d MB  (should drop back near baseline)\n", WSmb());

    Sleep(1500);
    wprintf(L"1.5s later (sustained):  %4d MB\n", WSmb());

    CoUninitialize();
    return 0;
}
