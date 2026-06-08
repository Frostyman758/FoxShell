#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include "FoxShellFolder.h"
#include "bridge.h"

// Extracting a file to disk so its associated app can open it. Browsing extracts
// nothing; this path runs ONLY when the user explicitly opens a leaf file.
//
// One managed cache root: %TEMP%\fox\<archive>\<...interior...>, mirroring the
// archive layout so names/extensions stay correct and the whole tree clears
// trivially. The cache is wiped once per Explorer session (PurgeExtractCacheOnce)
// so extracted copies never pile up across sessions.

static void PurgeExtractCacheOnce()
{
    static LONG done = 0;
    if (InterlockedExchange(&done, 1)) return;
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"fox";
    wchar_t from[MAX_PATH + 2] = {};
    lstrcpynW(from, dir.c_str(), MAX_PATH);   // double-null terminated for SHFileOperation
    SHFILEOPSTRUCTW op = {};
    op.wFunc  = FO_DELETE;
    op.pFrom  = from;
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

HRESULT FoxShellFolder::OpenChildFile(const wchar_t* leafName, HWND hwnd)
{
    PurgeExtractCacheOnce();
    std::wstring interior = ChildInteriorPath(leafName);

    // Read the file's bytes from the archive (in memory — no disk during browse).
    auto& br = Bridge::Get();
    bool owns = false;
    FoxArchive* h = br.OpenChain(m_archivePath, m_chain, owns);
    if (!h) return E_FAIL;
    uint8_t* data = nullptr; int64_t size = 0;
    int rc = br.read(h, interior.c_str(), &data, &size);
    br.ReleaseChain(h, owns);
    if (rc != FOXARC_OK) return E_FAIL;

    // %TEMP%\fox\<archive>\<...interior...> — "fox" to match the Fox Engine
    // convention (kept in sync with the rest of the toolset).
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const wchar_t* stem = wcsrchr(m_archivePath.c_str(), L'\\');
    std::wstring full = std::wstring(tmp) + L"fox\\" + (stem ? stem + 1 : m_archivePath.c_str()) + L"\\";
    std::wstring rel = interior;
    for (auto& c : rel) if (c == L'/') c = L'\\';
    full += rel;

    auto slash = full.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        SHCreateDirectoryExW(nullptr, full.substr(0, slash).c_str(), nullptr);

    HANDLE fh = CreateFileW(full.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool wrote = false;
    if (fh != INVALID_HANDLE_VALUE)
    {
        DWORD w = 0;
        wrote = (size == 0) || (WriteFile(fh, data, (DWORD)size, &w, nullptr) && w == (DWORD)size);
        CloseHandle(fh);
    }
    br.free_blob(data);
    if (!wrote) return E_FAIL;

    // Launch with the file's associated application (e.g. .lua -> the editor).
    ShellExecuteW(hwnd, nullptr, full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
