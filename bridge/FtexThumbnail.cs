using MgsvModBldr.Tools.Ftex.Dds;
using MgsvModBldr.Tools.Ftex.Dds.Enum;
using MgsvModBldr.Tools.Ftex.Ftex;
using MgsvModBldr.Tools.Ftex.Ftexs;

namespace MgsvModBldr.Tools.NativeBridge;

// Build a small single-mip DDS from a .ftex (+ its .ftexs sidecars) so the
// shell extension can hand it to WIC and show a texture thumbnail. The .ftex
// holds only the header + mip directory; pixel data lives in "<stem>.<N>.ftexs"
// siblings (mip.FtexsFileNumber). We pick a ~256px mip, read+decompress just
// that one, and wrap it in a 1-mip DDS — no need to assemble the whole texture.
internal static class FtexThumbnail
{
    // Archive source (NSE items). The interior path may cross nested-archive
    // boundaries (a .pftxs/.fpk packed inside the .dat) — resolve into the
    // innermost container first, then read the .ftex (+ .ftexs) from there.
    public static byte[]? Build(ArchiveHandle ar, string interiorFtexPath)
    {
        var opened = new List<ArchiveHandle>();
        try
        {
            var (inner, path) = ResolveNested(ar, interiorFtexPath, opened);
            return Build(p => { var n = inner.FindFile(p); return n is null ? null : inner.ReadFile(n); },
                         path);
        }
        finally { foreach (var h in opened) h.Dispose(); }
    }

    // Walk `interiorPath`; whenever a prefix names a nested archive, open it
    // in-memory and keep resolving inside it. Returns the innermost handle and
    // the target's path within it. Nested handles are appended to `opened`.
    // Only the nested container's bytes are read from the (possibly GB) parent —
    // never the whole archive — and they're disposed right after the thumbnail.
    private static (ArchiveHandle handle, string path) ResolveNested(
        ArchiveHandle root, string interiorPath, List<ArchiveHandle> opened)
    {
        var cur = root;
        var accum = "";
        foreach (var seg in interiorPath.Replace('\\', '/')
                                        .Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            accum = accum.Length == 0 ? seg : accum + "/" + seg;
            var node = cur.FindFile(accum);
            if (node is not null && node.IsArchive)
            {
                ArchiveHandle? nested = null;
                try { nested = ArchiveHandle.OpenNestedBytes(cur.ReadFile(node)); }
                catch { /* unknown/corrupt container — stop; FindFile will fail */ }
                if (nested is null) break;
                opened.Add(nested);
                cur = nested;
                accum = "";
            }
        }
        return (cur, accum);
    }

    // Loose-file source (real .ftex on disk + its .ftexs siblings in the dir).
    public static byte[]? BuildFromFiles(string ftexFullPath)
    {
        var dir = Path.GetDirectoryName(ftexFullPath) ?? ".";
        return Build(p =>
        {
            var f = Path.Combine(dir, Path.GetFileName(p.Replace('/', '\\')));
            return File.Exists(f) ? File.ReadAllBytes(f) : null;
        }, Path.GetFileName(ftexFullPath));
    }

    private static byte[]? Build(Func<string, byte[]?> read, string interiorPath)
    {
        // A .ftex previews the whole texture; a "<stem>.<N>.ftexs" previews only
        // the mip tier stored in that sidecar (its "individual part"). Either
        // way the .ftex HEADER (format/dimensions/mip directory) is required.
        string headerPath = interiorPath;
        int restrictFtexs = -1;                 // -1 = any (the .ftex itself)
        if (interiorPath.EndsWith(".ftexs", StringComparison.OrdinalIgnoreCase) &&
            TryParseFtexs(interiorPath, out var hdr, out var n))
        {
            headerPath = hdr;
            restrictFtexs = n;
        }

        byte[]? ftexBytes = read(headerPath);
        if (ftexBytes is null) return null;

        FtexFile ftex;
        using (var ms = new MemoryStream(ftexBytes, writable: false))
            ftex = FtexFile.ReadFtexFile(ms);

        // Among the candidate mips (all, or only those in the requested .ftexs),
        // pick the largest whose dimension is <= 256 (cheap, sharp thumbnail);
        // if every candidate is bigger, take the smallest one.
        FtexFileMipMapInfo? target = null;
        foreach (var m in ftex.MipMapInfos)
        {
            if (restrictFtexs >= 0 && m.FtexsFileNumber != restrictFtexs) continue;
            int dim = Math.Max(Math.Max(1, ftex.Width >> m.Index), Math.Max(1, ftex.Height >> m.Index));
            if (dim <= 256 && (target is null || m.Index < target.Index)) target = m;
        }
        if (target is null)
            foreach (var m in ftex.MipMapInfos)
            {
                if (restrictFtexs >= 0 && m.FtexsFileNumber != restrictFtexs) continue;
                if (target is null || m.Index > target.Index) target = m;
            }
        if (target is null) return null;

        // Where the chosen mip's bytes live (sibling .ftexs computed from the
        // .ftex header path, so this is correct whether we started from the
        // .ftex or one of its .ftexs).
        byte[]? sourceBytes;
        if (target.FtexsFileNumber == 0)
            sourceBytes = ftexBytes;                              // resident in the .ftex
        else
            sourceBytes = read(SiblingFtexs(headerPath, target.FtexsFileNumber));
        if (sourceBytes is null) return null;                    // sidecar missing

        var ftexs = new FtexsFile { FileNumber = target.FtexsFileNumber };
        using (var ms = new MemoryStream(sourceBytes, writable: false))
        {
            ms.Position = target.Offset;
            ftexs.Read(ms, target.ChunkCount, target.Offset, target.DecompressedFileSize);
        }
        byte[] mipData = ftexs.Data;                              // decompressed BC/raw bytes

        int w = Math.Max(1, ftex.Width >> target.Index);
        int h = Math.Max(1, ftex.Height >> target.Index);

        var dds = new DdsFile
        {
            Header = new DdsFileHeader
            {
                Size        = DdsFileHeader.DefaultHeaderSize,
                Flags       = DdsFileHeaderFlags.Texture,
                Width       = w,
                Height      = h,
                Depth       = 0,
                MipMapCount = 0,
                Caps        = DdsSurfaceFlags.Texture,
                PixelFormat = ftex.PixelFormatType switch
                {
                    0 => DdsPixelFormat.DdsPfA8R8G8B8(),
                    1 => DdsPixelFormat.DdsLuminance(),
                    2 => DdsPixelFormat.DdsPfDxt1(),
                    4 => DdsPixelFormat.DdsPfDxt5(),
                    _ => DdsPixelFormat.DdsPfDxt5(),
                },
            },
            Data = mipData,
        };

        using var os = new MemoryStream();
        dds.Write(os);
        return os.ToArray();
    }

    // "Assets/x/foo.1.ftexs" -> header "Assets/x/foo.ftex", n = 1
    private static bool TryParseFtexs(string ftexsPath, out string headerPath, out int n)
    {
        headerPath = ""; n = -1;
        var norm = ftexsPath.Replace('\\', '/');
        int slash = norm.LastIndexOf('/');
        string dir  = slash < 0 ? "" : norm[..(slash + 1)];
        string leaf = slash < 0 ? norm : norm[(slash + 1)..];
        if (!leaf.EndsWith(".ftexs", StringComparison.OrdinalIgnoreCase)) return false;
        string body = leaf[..^6];               // "foo.1"
        int dot = body.LastIndexOf('.');
        if (dot < 0 || !int.TryParse(body[(dot + 1)..], out n)) return false;
        headerPath = $"{dir}{body[..dot]}.ftex"; // "foo.ftex"
        return true;
    }

    // "Assets/x/foo.ftex" + 1 -> "Assets/x/foo.1.ftexs"
    private static string SiblingFtexs(string ftexPath, int n)
    {
        var norm = ftexPath.Replace('\\', '/');
        int slash = norm.LastIndexOf('/');
        string dir  = slash < 0 ? "" : norm[..(slash + 1)];
        string leaf = slash < 0 ? norm : norm[(slash + 1)..];
        if (leaf.EndsWith(".ftex", StringComparison.OrdinalIgnoreCase)) leaf = leaf[..^5];
        return $"{dir}{leaf}.{n}.ftexs";
    }
}
