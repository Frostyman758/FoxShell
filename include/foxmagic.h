/* ============================================================================
 * foxmagic.h — content sniffing for Fox Engine container formats.
 *
 * This is the C++ mirror of the bridge's authoritative detector,
 * bridge/FoxFormat.cs. The shell extension and foxbrowse use it to decide, by
 * CONTENT and not by file extension, whether a file is actually a Fox archive
 * we can browse.
 *
 * Why a separate, header-only copy instead of going through foxarchive.dll:
 *   - The check is a 16-byte read + a few comparisons; loading the NativeAOT
 *     bridge for every .dat Explorer hovers over would be wasteful.
 *   - It lets us decline a non-Fox file BEFORE the managed runtime is touched.
 *
 * Because of this, a generic .dat (a save game, a video, another app's data
 * file) is left alone: its bytes don't match a Fox magic, so the shell does not
 * mount it and foxbrowse falls back to the file's normal handler.
 *
 * Keep the magic here in lockstep with FoxFormat.Detect() in FoxFormat.cs.
 * ========================================================================== */
#ifndef FOXMAGIC_H
#define FOXMAGIC_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

enum class FoxFormat { Unknown = 0, Qar, Fpk, Fpkd, Pftxs, G0s };

/* Number of leading bytes Fox*Sniff needs (longest magic = 10, rounded up). */
static const size_t FOX_SNIFF_BYTES = 16;

/* .g0s (GZ QAR) has NO header — it ends with a 20-byte footer whose 4 bytes at
 * offset 4 are 0x71610000 and whose last 4 bytes are the footer size (20). This
 * footer check is what tells a real GZ archive (data_01/02.g0s) apart from a
 * non-archive of the same extension (data_00.g0s is a WMV video). */
static const uint32_t FOX_G0S_FOOTER_MAGIC = 0x71610000u;
static const size_t   FOX_G0S_FOOTER_BYTES = 20;

/* Identify a header-based Fox container from the first bytes of its content. */
inline FoxFormat FoxSniffBytes(const uint8_t* b, size_t n)
{
    if (n >= 4 && b[0] == 0x53 && b[1] == 0x51 && b[2] == 0x41 && b[3] == 0x52)
        return FoxFormat::Qar;                                   /* "SQAR" */

    if (n >= 10 && b[0] == 0x66 && b[1] == 0x6f && b[2] == 0x78 &&
        b[3] == 0x66 && b[4] == 0x70 && b[5] == 0x6b)            /* "foxfpk" */
        return (b[6] == 0x64) ? FoxFormat::Fpkd : FoxFormat::Fpk; /* 'd' vs '\0' */

    if (n >= 4 && b[0] == 0x50 && b[1] == 0x46 && b[2] == 0x54 && b[3] == 0x58)
        return FoxFormat::Pftxs;                                 /* "PFTX" */

    return FoxFormat::Unknown;
}

/* True if a 20-byte tail is a valid .g0s footer. */
inline bool FoxIsG0sFooter(const uint8_t* t, size_t n)
{
    if (n < FOX_G0S_FOOTER_BYTES) return false;
    uint32_t magic; uint32_t footerSize;
    memcpy(&magic, t + 4, 4);
    memcpy(&footerSize, t + 16, 4);
    return magic == FOX_G0S_FOOTER_MAGIC && footerSize == FOX_G0S_FOOTER_BYTES;
}

/* Identify a Fox container by peeking at a file on disk. Checks the header
 * magic first, then (for headerless .g0s) the trailing footer. Returns Unknown
 * on any open/read failure (caller treats that as "not a Fox archive"). */
inline FoxFormat FoxSniffFile(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return FoxFormat::Unknown;

    uint8_t head[FOX_SNIFF_BYTES] = {};
    DWORD got = 0;
    FoxFormat fmt = FoxFormat::Unknown;
    if (ReadFile(h, head, (DWORD)sizeof(head), &got, nullptr))
        fmt = FoxSniffBytes(head, got);

    if (fmt == FoxFormat::Unknown)              // try the .g0s footer
    {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(h, &size) && size.QuadPart >= (LONGLONG)FOX_G0S_FOOTER_BYTES)
        {
            LARGE_INTEGER off; off.QuadPart = size.QuadPart - (LONGLONG)FOX_G0S_FOOTER_BYTES;
            if (SetFilePointerEx(h, off, nullptr, FILE_BEGIN))
            {
                uint8_t tail[FOX_G0S_FOOTER_BYTES] = {};
                DWORD tgot = 0;
                if (ReadFile(h, tail, (DWORD)sizeof(tail), &tgot, nullptr) &&
                    FoxIsG0sFooter(tail, tgot))
                    fmt = FoxFormat::G0s;
            }
        }
    }

    CloseHandle(h);
    return fmt;
}

/* True when the file is a Fox container the shell extension can browse. */
inline bool FoxIsArchiveFile(const wchar_t* path)
{
    return FoxSniffFile(path) != FoxFormat::Unknown;
}

#endif /* FOXMAGIC_H */
