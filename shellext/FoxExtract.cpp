#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include "FoxShellFolder.h"
#include "bridge.h"

// Directory foxshellext.dll lives in (its sidecars — vgmstream-cli, dicts — sit
// next to it). Trailing backslash included.
static std::wstring SelfDir()
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(Bridge::Get().SelfModule(), buf, MAX_PATH);
    std::wstring s = buf;
    auto slash = s.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"" : s.substr(0, slash + 1);
}

// Decode a Wwise .wem to a standard .wav using the bundled vgmstream-cli, so it
// plays in any media player. Returns false (caller opens the raw .wem) if
// vgmstream isn't present or the decode fails.
static bool ConvertWemToWav(const std::wstring& wem, const std::wstring& wav)
{
    std::wstring dir = SelfDir() + L"vgmstream\\";   // bundled in its own subdir
    std::wstring exe = dir + L"vgmstream-cli.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"\"" + exe + L"\" -o \"" + wav + L"\" \"" + wem + L"\"";
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(0);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    // CWD = the dll dir so vgmstream-cli finds its codec DLLs.
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 60000);   // generous cap for long tracks
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0 && GetFileAttributesW(wav.c_str()) != INVALID_FILE_ATTRIBUTES;
}

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

    // A .wem (Wwise) is unplayable as-is; transparently decode it to a .wav with
    // the bundled vgmstream so opening it just plays. Fall back to the raw .wem
    // if vgmstream isn't available or fails.
    std::wstring toOpen = full;
    if (full.size() > 4 && _wcsicmp(full.c_str() + full.size() - 4, L".wem") == 0)
    {
        std::wstring wav = full.substr(0, full.size() - 4) + L".wav";
        if (ConvertWemToWav(full, wav)) toOpen = wav;
    }

    // Launch with the file's associated application (e.g. .lua -> the editor,
    // .wav -> VLC/Media Player).
    ShellExecuteW(hwnd, nullptr, toOpen.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}
