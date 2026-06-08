namespace MgsvModBldr.Tools.NativeBridge;

// ============================================================================
// FoxFormat — the single source of truth for "what Fox Engine file is this?".
//
// Detection is CONTENT-based (magic bytes), never extension-based. This is the
// whole reason a generic .dat (a save game, a video, some other app's data
// file) is NOT mistaken for a Fox archive: its bytes don't match a Fox magic,
// so Detect() returns Unknown and the shell leaves it alone.
//
// The C++ side mirrors the magic in include/foxmagic.h — keep the two in step.
// The extension metadata below is derived from the MGSV Modding Wiki "File
// Formats" page (Atvaark/FoxTool lineage); it is used for naming, the installer
// association list, and as a *hint* for which nested entries to probe.
// ============================================================================

// Content format of a Fox Engine file, identified by its magic signature.
internal enum FoxFormat
{
    Unknown = 0,
    Qar,   // QAR archive       — ".dat"/".qar"          magic "SQAR"
    Fpk,   // Fox Package       — ".fpk"                 magic "foxfpk\0win"
    Fpkd,  // Fox Package Data  — ".fpkd"                magic "foxfpkdwin"
    Pftxs, // Packed Fox Texture— ".pftxs"               magic "PFTX" (+ "TEXL")
    G0s,   // GZ QAR archive    — ".g0s"                 footer 0x71610000 (no header)
}

internal static class FoxFormats
{
    // Bytes we need to look at to recognise every magic above (longest = 10,
    // rounded up). Callers should hand Detect() at least this many bytes.
    public const int SniffBytes = 16;

    // ── Magic signatures (mirror of include/foxmagic.h) ──────────────────────
    private static ReadOnlySpan<byte> Sqar => "SQAR"u8;          // QAR
    private static ReadOnlySpan<byte> FoxFpk => "foxfpk"u8;      // FPK family prefix
    private static ReadOnlySpan<byte> Pftx => "PFTX"u8;          // PFTXS

    // Identify a file from the first bytes of its content.
    public static FoxFormat Detect(ReadOnlySpan<byte> head)
    {
        if (head.Length >= 4 && head[..4].SequenceEqual(Sqar))
            return FoxFormat.Qar;

        if (head.Length >= 10 && head[..6].SequenceEqual(FoxFpk))
            // "foxfpk\0win" => Fpk, "foxfpkdwin" => Fpkd. Byte 6 disambiguates.
            return head[6] == (byte)'d' ? FoxFormat.Fpkd : FoxFormat.Fpk;

        if (head.Length >= 4 && head[..4].SequenceEqual(Pftx))
            return FoxFormat.Pftxs;

        return FoxFormat.Unknown;
    }

    // A container is an archive we can open and browse INTO. Every format we
    // currently detect is a container; non-container Fox formats (fmdl, fox2,
    // lba, …) don't get their own magic here because we never drill into them.
    public static bool IsContainer(FoxFormat f) => f != FoxFormat.Unknown;

    // ── .g0s (GZ QAR) — footer-based, no leading magic ───────────────────────
    // A .g0s archive ends with a 20-byte footer: count|0x71610000|offset|0|20,
    // and the final 4 bytes are the footer size (20). We must check the FOOTER,
    // not the head, because .g0s has no header — and because the extension is
    // overloaded (data_00.g0s is a WMV video, data_01/02.g0s are archives), this
    // is what tells a real GZ archive apart from a same-extension non-archive.
    public const int FooterBytes = MgsvModBldr.Tools.G0s.G0sArchive.FooterSize;   // 20

    public static bool IsG0sFooter(ReadOnlySpan<byte> tail)
    {
        if (tail.Length < FooterBytes) return false;
        uint magic      = System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(tail.Slice(4, 4));
        int  footerSize = System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(tail.Slice(16, 4));
        return magic == MgsvModBldr.Tools.G0s.G0sArchive.FooterMagic1
            && footerSize == MgsvModBldr.Tools.G0s.G0sArchive.FooterSize;
    }

    // ── Extension metadata (from the MGSV Modding Wiki "File Formats" page) ───

    // Top-level extensions worth associating with the shell extension. These are
    // the Fox container formats this bridge can actually browse:
    //   .dat/.qar = QAR archive (the main MGSV archives)   [wiki: "dat", "qar"]
    //   .g0s      = QAR archive, Ground Zeroes variant     [wiki: "g0s"]
    //   .fpk/.fpkd= Fox Package / Fox Package Data         [wiki: "fpk", "fpkd"]
    //   .pftxs    = Packed Fox Textures                     [wiki: "pftxs"]
    // .dat is overloaded by countless non-Fox apps, but association is safe here
    // because mounting is gated on Detect() == a real Fox magic at runtime.
    public static readonly string[] TopLevelExtensions =
        { ".dat", ".qar", ".g0s", ".fpk", ".fpkd", ".pftxs" };

    // Nested entries we offer to drill into. The listing flags these by name as
    // a cheap hint; the actual open re-confirms by magic (OpenNestedBytes), so a
    // mislabelled entry simply fails to open rather than corrupting anything.
    // Per the wiki, archives nested inside a QAR/FPK are fpk/fpkd/pftxs.
    private static readonly HashSet<string> NestedContainerExts =
        new(StringComparer.OrdinalIgnoreCase)
        { ".fpk", ".fpkd", ".pftxs", ".dat", ".qar", ".g0s" };

    public static bool IsNestedContainer(string name)
    {
        int dot = name.LastIndexOf('.');
        return dot >= 0 && NestedContainerExts.Contains(name[dot..]);
    }

    // Nested containers INSIDE a GZ archive (.g0s). GZ fpk/fpkd and GZ pftxs each
    // have a dedicated GZ reader (GzFpkFile / GzPftxsFile), so all three browse.
    private static readonly HashSet<string> GzNestedContainerExts =
        new(StringComparer.OrdinalIgnoreCase)
        { ".fpk", ".fpkd", ".pftxs", ".dat", ".qar", ".g0s" };

    public static bool IsGzNestedContainer(string name)
    {
        int dot = name.LastIndexOf('.');
        return dot >= 0 && GzNestedContainerExts.Contains(name[dot..]);
    }
}
