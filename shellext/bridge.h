#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "../include/foxarchive.h"

// Thin loader + handle cache around foxarchive.dll (the NativeAOT bridge).
// foxarchive.dll lives next to foxshellext.dll; we load it lazily on first use
// and tell it where qar_dictionary.txt is (also next to us).
class Bridge
{
public:
    static Bridge& Get()
    {
        static Bridge inst;
        return inst;
    }

    bool EnsureLoaded()
    {
        EnterCriticalSection(&m_cs);
        bool ok = DoLoad();
        LeaveCriticalSection(&m_cs);
        return ok;
    }

    // Function pointers (valid after EnsureLoaded()).
    int32_t (*open)(const wchar_t*, FoxArchive**) = nullptr;
    int32_t (*open_nested)(FoxArchive*, const wchar_t*, FoxArchive**) = nullptr;
    void    (*close)(FoxArchive*) = nullptr;
    int32_t (*list)(FoxArchive*, const wchar_t*, FoxList**) = nullptr;
    int32_t (*list_count)(FoxList*, int32_t*) = nullptr;
    int32_t (*list_item)(FoxList*, int32_t, FoxItemInfo*) = nullptr;
    void    (*list_free)(FoxList*) = nullptr;
    int32_t (*read)(FoxArchive*, const wchar_t*, uint8_t**, int64_t*) = nullptr;
    int32_t (*ftex_thumb)(FoxArchive*, const wchar_t*, uint8_t**, int64_t*) = nullptr;
    int32_t (*ftex_thumb_path)(const wchar_t*, uint8_t**, int64_t*) = nullptr;
    void    (*free_blob)(uint8_t*) = nullptr;
    void    (*trim)(void) = nullptr;   // force GC + return memory to the OS
    void    (*idle)(void) = nullptr;   // trim + drop the name dictionaries

    // Resolve the innermost archive for archivePath + nested chain. Returns the
    // handle (or null) and sets owns=true if the caller must close it (i.e. it
    // is a nested handle, not the cached top-level one).
    FoxArchive* OpenChain(const std::wstring& archivePath,
                          const std::vector<std::wstring>& chain, bool& owns)
    {
        owns = false;
        if (!EnsureLoaded()) return nullptr;

        FoxArchive* top = GetCachedTop(archivePath);
        if (!top) return nullptr;
        if (chain.empty()) return top;

        FoxArchive* cur = top;
        bool curOwned = false;
        for (const auto& seg : chain)
        {
            FoxArchive* next = nullptr;
            int rc = open_nested(cur, seg.c_str(), &next);
            if (curOwned) close(cur);
            if (rc != FOXARC_OK || !next) return nullptr;
            cur = next; curOwned = true;
        }
        owns = curOwned;
        return cur;
    }

    void ReleaseChain(FoxArchive* h, bool owns) { if (owns && h) close(h); }

    // Reference-count the cached top-level handle by the number of live
    // FoxShellFolder objects browsing that archive. A folder calls Acquire when
    // it learns its archive path and Release when it's destroyed. When the count
    // hits zero — i.e. the user has navigated away / closed the archive — the
    // cached handle is closed, evicted, and a GC trim returns the (possibly
    // large) browse-time memory to the OS instead of holding it until Explorer
    // restarts.
    void AcquireArchive(const std::wstring& path)
    {
        if (path.empty()) return;
        std::wstring key = Lower(path);
        EnterCriticalSection(&m_cs);
        if (++m_topCache[key].refs == 1) m_live++;   // first browser of this archive
        LeaveCriticalSection(&m_cs);
    }

    void ReleaseArchive(const std::wstring& path)
    {
        if (path.empty() || !EnsureLoaded()) return;
        std::wstring key = Lower(path);
        FoxArchive* toClose = nullptr;
        bool lastArchive = false, freed = false;
        EnterCriticalSection(&m_cs);
        auto it = m_topCache.find(key);
        if (it != m_topCache.end() && --it->second.refs <= 0)
        {
            toClose = it->second.handle;
            m_topCache.erase(it);
            freed = true;
            if (--m_live <= 0) { m_live = 0; lastArchive = true; }
        }
        LeaveCriticalSection(&m_cs);
        // Close + trim OUTSIDE the lock (managed calls). When the LAST archive is
        // gone, also drop the name dictionaries (idle); otherwise just trim.
        if (toClose && close) close(toClose);
        if (!freed) return;
        if (lastArchive && idle) idle();
        else if (trim)           trim();
    }

    HMODULE SelfModule() const { return m_self; }

private:
    static std::wstring Lower(const std::wstring& s)
    { std::wstring k = s; for (auto& c : k) c = (wchar_t)towlower(c); return k; }

    Bridge() { InitializeCriticalSection(&m_cs); }

    bool DoLoad()
    {
        if (m_loaded) return m_dll != nullptr;
        m_loaded = true;

        wchar_t dir[MAX_PATH];
        GetModuleFileNameW(m_self, dir, MAX_PATH);
        wchar_t* slash = wcsrchr(dir, L'\\');
        if (slash) *(slash + 1) = 0;

        std::wstring dllPath = std::wstring(dir) + L"foxarchive.dll";
        m_dll = LoadLibraryW(dllPath.c_str());
        if (!m_dll) return false;

        open        = (decltype(open))        GetProcAddress(m_dll, "foxarc_open");
        open_nested = (decltype(open_nested)) GetProcAddress(m_dll, "foxarc_open_nested");
        close       = (decltype(close))       GetProcAddress(m_dll, "foxarc_close");
        list        = (decltype(list))        GetProcAddress(m_dll, "foxarc_list");
        list_count  = (decltype(list_count))  GetProcAddress(m_dll, "foxarc_list_count");
        list_item   = (decltype(list_item))   GetProcAddress(m_dll, "foxarc_list_item");
        list_free   = (decltype(list_free))   GetProcAddress(m_dll, "foxarc_list_free");
        read        = (decltype(read))        GetProcAddress(m_dll, "foxarc_read");
        ftex_thumb      = (decltype(ftex_thumb))      GetProcAddress(m_dll, "foxarc_ftex_thumb");
        ftex_thumb_path = (decltype(ftex_thumb_path)) GetProcAddress(m_dll, "foxarc_ftex_thumb_path");
        free_blob   = (decltype(free_blob))   GetProcAddress(m_dll, "foxarc_free_blob");
        trim        = (decltype(trim))        GetProcAddress(m_dll, "foxarc_trim");
        idle        = (decltype(idle))        GetProcAddress(m_dll, "foxarc_idle");

        auto setDict = (void(*)(const wchar_t*)) GetProcAddress(m_dll, "foxarc_set_dict_dir");
        if (setDict)
        {
            std::wstring d(dir);
            if (!d.empty() && d.back() == L'\\') d.pop_back();
            setDict(d.c_str());
        }
        return open && list && read;
    }

    FoxArchive* GetCachedTop(const std::wstring& path)
    {
        std::wstring key = Lower(path);

        EnterCriticalSection(&m_cs);
        auto it = m_topCache.find(key);
        FoxArchive* h = (it != m_topCache.end()) ? it->second.handle : nullptr;
        LeaveCriticalSection(&m_cs);
        if (h) return h;

        FoxArchive* opened = nullptr;
        if (open(path.c_str(), &opened) != FOXARC_OK || !opened) return nullptr;

        EnterCriticalSection(&m_cs);
        auto& e = m_topCache[key];           // entry already exists (folder Acquired)
        if (e.handle) { LeaveCriticalSection(&m_cs); close(opened); return e.handle; } // another thread won
        e.handle = opened;
        LeaveCriticalSection(&m_cs);
        return opened;
    }

    struct CacheEntry { FoxArchive* handle = nullptr; int refs = 0; };

    HMODULE m_self = nullptr; // set by DllMain
    HMODULE m_dll  = nullptr;
    bool    m_loaded = false;
    int     m_live = 0;     // number of archives with at least one live folder
    CRITICAL_SECTION m_cs;
    std::map<std::wstring, CacheEntry> m_topCache;

public:
    void SetSelf(HMODULE h) { m_self = h; }
};
