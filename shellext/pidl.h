#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <string>

// Our own SHITEMID payload for one interior entry of an archive. Explorer
// stores these opaquely in PIDLs; only we parse them. A 'FX' signature lets us
// recognise our items inside a mixed (filesystem + ours) absolute PIDL.
#pragma pack(push, 1)
struct FoxItemID
{
    USHORT    cb;        // byte size of this SHITEMID incl. cb (SHITEMID contract)
    USHORT    sig;       // FOX_SIG
    BYTE      kind;      // FoxKind
    BYTE      _pad;
    ULONGLONG size;      // uncompressed size (0 for folders)
    ULONGLONG hash;      // QAR path hash, or 0
    WCHAR     name[1];   // null-terminated leaf name, variable length
};
#pragma pack(pop)

enum FoxKind : BYTE { FOX_FILE = 0, FOX_FOLDER = 1, FOX_ARCHIVE = 2 };
static const USHORT FOX_SIG = 0x5846; // 'FX'

// View a (LP)CITEMIDLIST's first SHITEMID as a FoxItemID, or null if it isn't ours.
inline const FoxItemID* FoxFromItem(LPCITEMIDLIST pidl)
{
    if (!pidl || pidl->mkid.cb < sizeof(FoxItemID)) return nullptr;
    auto* f = reinterpret_cast<const FoxItemID*>(pidl);
    return f->sig == FOX_SIG ? f : nullptr;
}

// Allocate a single-item relative PIDL (one FoxItemID + 2-byte terminator).
inline LPITEMIDLIST FoxCreateItem(FoxKind kind, const wchar_t* name,
                                  ULONGLONG size, ULONGLONG hash)
{
    size_t nameChars = wcslen(name) + 1;
    size_t idBytes   = offsetof(FoxItemID, name) + nameChars * sizeof(WCHAR);
    size_t total     = idBytes + sizeof(USHORT); // + null terminator SHITEMID
    auto* p = static_cast<BYTE*>(CoTaskMemAlloc(total));
    if (!p) return nullptr;
    ZeroMemory(p, total);
    auto* f = reinterpret_cast<FoxItemID*>(p);
    f->cb   = static_cast<USHORT>(idBytes);
    f->sig  = FOX_SIG;
    f->kind = static_cast<BYTE>(kind);
    f->size = size;
    f->hash = hash;
    wcscpy_s(f->name, nameChars, name);
    // trailing USHORT already zero -> list terminator
    return reinterpret_cast<LPITEMIDLIST>(p);
}

// Next SHITEMID in a list, or null at the terminator.
inline LPCITEMIDLIST FoxNext(LPCITEMIDLIST pidl)
{
    if (!pidl || pidl->mkid.cb == 0) return nullptr;
    auto* p = reinterpret_cast<const BYTE*>(pidl) + pidl->mkid.cb;
    auto* n = reinterpret_cast<LPCITEMIDLIST>(p);
    return n->mkid.cb == 0 ? nullptr : n;
}

inline bool FoxIsEmpty(LPCITEMIDLIST pidl) { return !pidl || pidl->mkid.cb == 0; }
