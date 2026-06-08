// Native smoke test for the foxarchive C ABI. Loads foxarchive.dll, opens a
// real archive, lists the root, reads a file, and (if present) drills into a
// nested archive. This is exactly how the shell extension consumes the bridge,
// but a bug here can't take down explorer.exe.
//
// Usage: abi_smoke.exe <foxarchive.dll> <dictDir> <archive> [archive2 ...]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "../include/foxarchive.h"

typedef int32_t (*pfn_abi)(void);
typedef void    (*pfn_setdict)(const wchar_t*);
typedef int32_t (*pfn_open)(const wchar_t*, FoxArchive**);
typedef int32_t (*pfn_opennested)(FoxArchive*, const wchar_t*, FoxArchive**);
typedef void    (*pfn_close)(FoxArchive*);
typedef int32_t (*pfn_list)(FoxArchive*, const wchar_t*, FoxList**);
typedef int32_t (*pfn_listcount)(FoxList*, int32_t*);
typedef int32_t (*pfn_listitem)(FoxList*, int32_t, FoxItemInfo*);
typedef void    (*pfn_listfree)(FoxList*);
typedef int32_t (*pfn_read)(FoxArchive*, const wchar_t*, uint8_t**, int64_t*);
typedef void    (*pfn_freeblob)(uint8_t*);

static pfn_open       fOpen;
static pfn_opennested fOpenNested;
static pfn_close      fClose;
static pfn_list       fList;
static pfn_listcount  fListCount;
static pfn_listitem   fListItem;
static pfn_listfree   fListFree;
static pfn_read       fRead;
static pfn_freeblob   fFreeBlob;

// List one directory; return the interior path of the first nested archive seen.
static bool dumpDir(FoxArchive* a, const wchar_t* dir, wchar_t* firstArchiveOut, size_t cap)
{
    FoxList* list = nullptr;
    int rc = fList(a, dir, &list);
    if (rc != FOXARC_OK) { wprintf(L"  [list '%s' failed rc=%d]\n", dir, rc); return false; }
    int n = 0; fListCount(list, &n);
    wprintf(L"  '%s' -> %d children\n", dir[0] ? dir : L"(root)", n);
    bool found = false;
    for (int i = 0; i < n && i < 40; i++)
    {
        FoxItemInfo info{};
        if (fListItem(list, i, &info) != FOXARC_OK) continue;
        wprintf(L"    %-3d %s  %-44s  %llu bytes%s\n",
                i,
                info.isFolder ? L"[D]" : (info.isArchive ? L"[A]" : L"   "),
                info.name,
                (unsigned long long)info.size,
                info.isArchive ? L"  <nested archive>" : L"");
        if (info.isArchive && !found && firstArchiveOut)
        {
            if (dir[0]) swprintf(firstArchiveOut, cap, L"%s/%s", dir, info.name);
            else        swprintf(firstArchiveOut, cap, L"%s", info.name);
            found = true;
        }
    }
    if (n > 40) wprintf(L"    ... (%d more)\n", n - 40);
    fListFree(list);
    return found;
}

// Read the first file (non-folder) found at root and print a preview.
static void readFirstFile(FoxArchive* a)
{
    FoxList* list = nullptr;
    if (fList(a, L"", &list) != FOXARC_OK) return;
    int n = 0; fListCount(list, &n);
    for (int i = 0; i < n; i++)
    {
        FoxItemInfo info{};
        fListItem(list, i, &info);
        if (info.isFolder) continue;
        uint8_t* data = nullptr; int64_t size = 0;
        int rc = fRead(a, info.name, &data, &size);
        wprintf(L"  read '%s' rc=%d size=%lld first bytes:", info.name, rc, (long long)size);
        if (rc == FOXARC_OK)
        {
            for (int b = 0; b < 8 && b < size; b++) wprintf(L" %02X", data[b]);
            fFreeBlob(data);
        }
        wprintf(L"\n");
        break;
    }
    fListFree(list);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) { wprintf(L"usage: abi_smoke <dll> <dictDir> <archive>...\n"); return 2; }

    HMODULE m = LoadLibraryW(argv[1]);
    if (!m) { wprintf(L"LoadLibrary failed: %lu\n", GetLastError()); return 1; }

    auto fAbi     = (pfn_abi)     GetProcAddress(m, "foxarc_abi_version");
    auto fSetDict = (pfn_setdict) GetProcAddress(m, "foxarc_set_dict_dir");
    fOpen       = (pfn_open)       GetProcAddress(m, "foxarc_open");
    fOpenNested = (pfn_opennested) GetProcAddress(m, "foxarc_open_nested");
    fClose      = (pfn_close)      GetProcAddress(m, "foxarc_close");
    fList       = (pfn_list)       GetProcAddress(m, "foxarc_list");
    fListCount  = (pfn_listcount)  GetProcAddress(m, "foxarc_list_count");
    fListItem   = (pfn_listitem)   GetProcAddress(m, "foxarc_list_item");
    fListFree   = (pfn_listfree)   GetProcAddress(m, "foxarc_list_free");
    fRead       = (pfn_read)       GetProcAddress(m, "foxarc_read");
    fFreeBlob   = (pfn_freeblob)   GetProcAddress(m, "foxarc_free_blob");

    if (!fAbi || !fOpen || !fList || !fRead) { wprintf(L"missing exports\n"); return 1; }
    wprintf(L"ABI version: %d\n", fAbi());
    fSetDict(argv[2]);

    for (int ai = 3; ai < argc; ai++)
    {
        wprintf(L"\n=== %s ===\n", argv[ai]);
        FoxArchive* a = nullptr;
        int rc = fOpen(argv[ai], &a);
        if (rc != FOXARC_OK) { wprintf(L"  open failed rc=%d\n", rc); continue; }

        wchar_t nested[1024] = L"";
        bool hasNested = dumpDir(a, L"", nested, 1024);
        // probe a couple of common subdirs for a nested archive too
        if (!hasNested) { wchar_t buf[1024]=L""; if (dumpDir(a, L"Assets", buf, 1024)) { wcscpy_s(nested, buf); hasNested=true; } }
        readFirstFile(a);

        if (hasNested)
        {
            wprintf(L"  drilling into nested archive: %s\n", nested);
            FoxArchive* inner = nullptr;
            int nrc = fOpenNested(a, nested, &inner);
            if (nrc == FOXARC_OK)
            {
                wchar_t dummy[8]=L"";
                dumpDir(inner, L"", dummy, 8);
                readFirstFile(inner);
                fClose(inner);
            }
            else wprintf(L"  open_nested rc=%d\n", nrc);
        }
        fClose(a);
    }
    FreeLibrary(m);
    return 0;
}
