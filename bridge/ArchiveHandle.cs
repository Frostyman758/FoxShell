using MgsvModBldr.Tools.Qar;
using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Fpk.Gz;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Pftxs.Gz;
using MgsvModBldr.Tools.G0s;

namespace MgsvModBldr.Tools.NativeBridge;

// In-memory browsable model of one opened archive (top-level or nested).
//
// Opening is CONTENT-driven: we sniff the first bytes with FoxFormats.Detect
// and dispatch to the matching reader. There is no extension check, so any path
// whose bytes are a real Fox magic browses, and anything else is rejected with
// InvalidDataException (the shell turns that into "not a Fox archive").
//
// Tree construction lives in ArchiveHandle.Tree.cs; the QAR name dictionary in
// QarNameDictionary.cs. This file owns lifetime, opening, and reading.
internal sealed partial class ArchiveHandle : IDisposable
{
    internal enum Kind { Qar, Fpk, GzFpk, Pftxs, GzPftxs, G0s }

    private readonly Kind _kind;
    private readonly string? _filePath;   // top-level: reopen per read
    private readonly byte[]? _rawBytes;    // nested: backing bytes (no copy on reopen)
    private readonly QarFile? _qar;
    private G0sArchive? _g0s;              // G0s index (footer + entry table)
    private readonly object _readLock = new();

    public DirNode Root { get; } = new();

    private ArchiveHandle(Kind kind, string? filePath, byte[]? rawBytes, QarFile? qar)
    {
        _kind = kind; _filePath = filePath; _rawBytes = rawBytes; _qar = qar;
    }

    private Stream OpenSource()
        => _filePath is not null ? File.OpenRead(_filePath)
                                 : new MemoryStream(_rawBytes!, writable: false);

    // ── Opening ────────────────────────────────────────────────────────────

    public static ArchiveHandle OpenPath(string path)
    {
        Span<byte> magic = stackalloc byte[FoxFormats.SniffBytes];
        FoxFormat fmt;
        using (var fs = File.OpenRead(path))
        {
            int n = ReadAtMost(fs, magic);
            fmt = FoxFormats.Detect(magic[..n]);
            if (fmt == FoxFormat.Unknown)        // .g0s has no header — check the footer
            {
                Span<byte> tail = stackalloc byte[FoxFormats.FooterBytes];
                if (ReadTail(fs, tail) == tail.Length && FoxFormats.IsG0sFooter(tail))
                    fmt = FoxFormat.G0s;
            }
        }

        return fmt switch
        {
            FoxFormat.Qar => OpenQar(path, null),
            FoxFormat.Fpk or FoxFormat.Fpkd => OpenFpk(path, null),
            FoxFormat.Pftxs => OpenPftxs(path, null),
            FoxFormat.G0s => OpenG0s(path, null),
            _ => throw new InvalidDataException("not a QAR/FPK/PFTXS/G0S archive"),
        };
    }

    public static ArchiveHandle OpenNestedBytes(byte[] bytes)
    {
        var magic = bytes.AsSpan(0, Math.Min(bytes.Length, FoxFormats.SniffBytes));
        var fmt = FoxFormats.Detect(magic);
        if (fmt == FoxFormat.Unknown
            && bytes.Length >= FoxFormats.FooterBytes
            && FoxFormats.IsG0sFooter(bytes.AsSpan(bytes.Length - FoxFormats.FooterBytes)))
            fmt = FoxFormat.G0s;

        return fmt switch
        {
            FoxFormat.Qar => OpenQar(null, bytes),
            FoxFormat.Fpk or FoxFormat.Fpkd => OpenFpk(null, bytes),
            FoxFormat.Pftxs => OpenPftxs(null, bytes),
            FoxFormat.G0s => OpenG0s(null, bytes),
            _ => throw new InvalidDataException("nested blob is not a QAR/FPK/PFTXS/G0S archive"),
        };
    }

    // path != null => top-level (reopened per read); bytes != null => nested.

    private static ArchiveHandle OpenQar(string? path, byte[]? bytes)
    {
        var qar = new QarFile();
        if (path is not null)
        {
            qar.ReadFrom(path);          // headers only — cheap, no entry bodies
            qar.Close();                 // release its FileStream; we reopen per read
        }
        else
        {
            using var ms = new MemoryStream(bytes!, writable: false);
            qar.Read(ms);
            qar.Close();
        }
        var h = new ArchiveHandle(Kind.Qar, path, bytes, qar);
        h.BuildQarTree();
        return h;
    }

    private static ArchiveHandle OpenFpk(string? path, byte[]? bytes)
    {
        // Route GZ "ste" fpk to the dedicated GZ reader — the TPP FpkFile below
        // is left completely untouched. (GZ fpk only appears nested in a .g0s.)
        Span<byte> head = stackalloc byte[10];
        PeekHead(path, bytes, head);
        if (GzFpkFile.IsGzMagic(head)) return OpenGzFpk(path, bytes);

        var fpk = new FpkFile();
        if (path is not null) fpk.ReadFrom(path);     // FPK loads all entry data in memory
        else { using var ms = new MemoryStream(bytes!, writable: false); fpk.Read(ms); }
        var h = new ArchiveHandle(Kind.Fpk, path, bytes, null);
        h.BuildFpkTree(fpk);
        return h;
    }

    private static ArchiveHandle OpenGzFpk(string? path, byte[]? bytes)
    {
        GzFpkFile fpk;
        if (path is not null) { using var fs = File.OpenRead(path); fpk = GzFpkFile.Read(fs); }
        else { using var ms = new MemoryStream(bytes!, writable: false); fpk = GzFpkFile.Read(ms); }
        var h = new ArchiveHandle(Kind.GzFpk, path, bytes, null);
        h.BuildGzFpkTree(fpk);
        return h;
    }

    private static ArchiveHandle OpenPftxs(string? path, byte[]? bytes)
    {
        // GZ pftxs (float 1.0 at offset 4) uses a different layout — route it to
        // the dedicated GZ reader; the TPP PftxsFile below is left untouched.
        Span<byte> head = stackalloc byte[8];
        PeekHead(path, bytes, head);
        if (GzPftxsFile.IsGzPftxs(head)) return OpenGzPftxs(path, bytes);

        var pftxs = new PftxsFile();
        if (path is not null) pftxs.ReadFrom(path);   // loads all texture data in memory
        else { using var ms = new MemoryStream(bytes!, writable: false); pftxs.Read(ms); }
        var h = new ArchiveHandle(Kind.Pftxs, path, bytes, null);
        h.BuildPftxsTree(pftxs);
        return h;
    }

    private static ArchiveHandle OpenGzPftxs(string? path, byte[]? bytes)
    {
        GzPftxsFile pftxs;
        if (path is not null) { using var fs = File.OpenRead(path); pftxs = GzPftxsFile.Read(fs); }
        else { using var ms = new MemoryStream(bytes!, writable: false); pftxs = GzPftxsFile.Read(ms); }
        var h = new ArchiveHandle(Kind.GzPftxs, path, bytes, null);
        h.BuildGzPftxsTree(pftxs);
        return h;
    }

    private static ArchiveHandle OpenG0s(string? path, byte[]? bytes)
    {
        G0sArchive arc;
        if (path is not null) { using var fs = File.OpenRead(path); arc = G0sArchive.ReadIndex(fs); }
        else { using var ms = new MemoryStream(bytes!, writable: false); arc = G0sArchive.ReadIndex(ms); }
        var h = new ArchiveHandle(Kind.G0s, path, bytes, null) { _g0s = arc };
        h.BuildG0sTree();
        return h;
    }

    // ── Listing / navigation ─────────────────────────────────────────────────

    public DirNode? NavigateDir(string dirPath)
    {
        var dir = Root;
        if (string.IsNullOrEmpty(dirPath)) return dir;
        foreach (var part in dirPath.Replace('\\', '/').Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            if (!dir.Dirs.TryGetValue(part, out var child)) return null;
            dir = child;
        }
        return dir;
    }

    public FileNode? FindFile(string interiorPath)
    {
        var norm = interiorPath.Replace('\\', '/').TrimStart('/');
        int slash = norm.LastIndexOf('/');
        var dirPart = slash < 0 ? "" : norm[..slash];
        var leaf = slash < 0 ? norm : norm[(slash + 1)..];
        var dir = NavigateDir(dirPart);
        if (dir is null) return null;
        foreach (var f in dir.Files)
            if (string.Equals(f.Name, leaf, StringComparison.OrdinalIgnoreCase)) return f;
        return null;
    }

    // ── Reading ──────────────────────────────────────────────────────────────

    public byte[] ReadFile(FileNode node)
    {
        if (_kind == Kind.Fpk)
            return node.Fpk!.Data;       // already decrypted in memory
        if (_kind == Kind.GzFpk)
            return node.GzFpk!.Data;     // GZ fpk data, already in memory (plaintext)
        if (_kind == Kind.Pftxs)
            return node.Pftxs!.Data;     // texture bytes, already in memory
        if (_kind == Kind.GzPftxs)
            return node.GzPftxs!.Data;   // GZ texture bytes, already in memory

        if (_kind == Kind.G0s)           // GZ QAR: read raw blob, then decrypt
        {
            var e = node.G0s!;
            var raw = new byte[e.Size];
            lock (_readLock)
            {
                using var s = OpenSource();
                s.Seek(16L * e.Offset, SeekOrigin.Begin);
                ReadExactInto(s, raw);
            }
            // Decrypt mutates `raw` (outer pass) then unwraps the inner cipher if
            // present; we discard the inner key (read-only browsing).
            var (data, _) = G0sArchive.Decrypt(raw, e.Offset);
            return data;
        }

        lock (_readLock)                 // QAR: reopen + lazy-decrypt this entry
        {
            using var s = OpenSource();
            node.Qar!.ReadData(s);
            var data = node.Qar.Data;
            // Release the bytes immediately — do NOT retain file data on the
            // cached index. The cache holds only the table of contents (entry
            // names/offsets/sizes), never the (potentially GB) file contents.
            node.Qar.Data = System.Array.Empty<byte>();
            node.Qar.Loaded = false;
            return data;
        }
    }

    private static int ReadAtMost(Stream s, Span<byte> buf)
    {
        int n = 0;
        while (n < buf.Length)
        {
            int r = s.Read(buf[n..]);
            if (r == 0) break;
            n += r;
        }
        return n;
    }

    // Copy the first buf.Length bytes from a nested blob or a file on disk (used
    // to tell GZ "ste" fpk apart from TPP "win" fpk before choosing a reader).
    private static void PeekHead(string? path, byte[]? bytes, Span<byte> buf)
    {
        buf.Clear();
        if (bytes is not null)
        {
            bytes.AsSpan(0, Math.Min(bytes.Length, buf.Length)).CopyTo(buf);
            return;
        }
        using var fs = File.OpenRead(path!);
        ReadAtMost(fs, buf);
    }

    // Read the last buf.Length bytes of a seekable stream (for footer sniffing).
    private static int ReadTail(Stream s, Span<byte> buf)
    {
        if (!s.CanSeek || s.Length < buf.Length) return 0;
        s.Seek(-buf.Length, SeekOrigin.End);
        return ReadAtMost(s, buf);
    }

    private static void ReadExactInto(Stream s, Span<byte> buf)
    {
        if (ReadAtMost(s, buf) != buf.Length) throw new EndOfStreamException();
    }

    public void Dispose() => _qar?.Close();
}
