using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Sbp;
using MgsvModBldr.Tools.Stp;
using MgsvModBldr.Tools.Fsop;
using MgsvModBldr.Tools.Mtar;
using MgsvModBldr.Tools.NativeBridge;   // Lazy{Fpk,Pftxs,Sbp,Stp,Sab,Fsop,Mtar}Reader (compiled in)

// Lazy-vs-eager byte-equality check for the rel2.5 lazy fpk path.
//
// For each fpk/fpkd fixture: read it with Fox_parser's eager FpkFile (the
// oracle — loads & decrypts every entry up front) and with the bridge's
// LazyFpkReader (index only) + the same on-demand Decode the bridge runs in
// ReadFile. Every entry's final bytes must match exactly.

static byte[] Decode(byte[] b, string key)
{
    // Identical to ArchiveHandle.Decode(.., LazyBlob.Fpk) and FpkEntry.ReadData.
    if (b.Length > 0 && (b[0] == 0x1B || b[0] == 0x1C)
        && FpkCrypto.TryDecrypt(b, key, out var dec)) return dec;
    return b;
}

var dir = args.Length > 0 ? args[0] : @"C:\rsearch\test_fixtures\fpk";
var files = Directory.GetFiles(dir, "*.fpk").Concat(Directory.GetFiles(dir, "*.fpkd"))
                     .OrderBy(p => p).ToArray();

int totalEntries = 0, mismatches = 0, fileFails = 0;
foreach (var path in files)
{
    // Eager oracle: path -> decrypted bytes.
    var eager = new Dictionary<string, byte[]>(StringComparer.Ordinal);
    using (var fs = File.OpenRead(path))
    {
        var fpk = new FpkFile();
        fpk.Read(fs);
        foreach (var e in fpk.Entries) eager[e.FilePath.Data] = e.Data;
    }

    // Lazy: index only, then pull + decode each region on demand.
    var lazy = new Dictionary<string, byte[]>(StringComparer.Ordinal);
    using (var fs = File.OpenRead(path))
    {
        var entries = LazyFpkReader.Read(fs);
        foreach (var en in entries)
        {
            fs.Position = en.DataOffset;
            var raw = new byte[en.DataSize];
            int n = 0; while (n < raw.Length) { int r = fs.Read(raw, n, raw.Length - n); if (r == 0) break; n += r; }
            lazy[en.Path] = Decode(raw, en.Path);
        }
    }

    bool ok = true;
    if (eager.Count != lazy.Count) { ok = false; Console.WriteLine($"  COUNT {eager.Count} eager vs {lazy.Count} lazy"); }
    foreach (var kv in eager)
    {
        totalEntries++;
        if (!lazy.TryGetValue(kv.Key, out var lb)) { ok = false; mismatches++; Console.WriteLine($"  MISSING {kv.Key}"); continue; }
        if (!kv.Value.AsSpan().SequenceEqual(lb)) { ok = false; mismatches++; Console.WriteLine($"  DIFF {kv.Key} ({kv.Value.Length} vs {lb.Length})"); }
    }
    if (!ok) fileFails++;
    Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {eager.Count,4} entries");
}

Console.WriteLine($"--- FPK/FPKD: {files.Length} files, {totalEntries} entries, {mismatches} mismatched ---");

// ── PFTXS ────────────────────────────────────────────────────────────────────
var pdir = args.Length > 1 ? args[1] : @"C:\rsearch\test_fixtures";
var pfiles = Directory.GetFiles(pdir, "*.pftxs", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
int pEntries = 0, pMism = 0, pFails = 0;
foreach (var path in pfiles)
{
    // Eager oracle keeps groups; flatten to (groupIdx, hash) -> bytes in read order.
    var eager = new List<(ulong hash, byte[] data)>();
    using (var fs = File.OpenRead(path))
    {
        var pf = new PftxsFile();
        try { pf.Read(fs); } catch { Console.WriteLine($"SKIP  {Path.GetFileName(path)} (GZ/other)"); continue; }
        foreach (var g in pf.Groups) foreach (var e in g.Entries) eager.Add((e.Hash, e.Data));
    }

    var lazy = new List<(ulong hash, byte[] data)>();
    using (var fs = File.OpenRead(path))
    {
        foreach (var en in LazyPftxsReader.Read(fs))
        {
            fs.Position = en.DataOffset;
            var raw = new byte[en.DataSize];
            int n = 0; while (n < raw.Length) { int r = fs.Read(raw, n, raw.Length - n); if (r == 0) break; n += r; }
            lazy.Add((en.Hash, raw));
        }
    }

    bool ok = eager.Count == lazy.Count;
    int lim = Math.Min(eager.Count, lazy.Count);
    for (int i = 0; i < lim; i++)
    {
        pEntries++;
        if (eager[i].hash != lazy[i].hash || !eager[i].data.AsSpan().SequenceEqual(lazy[i].data))
        { ok = false; pMism++; Console.WriteLine($"  DIFF [{i}] {eager[i].hash:x16} ({eager[i].data.Length} vs {lazy[i].data.Length})"); }
    }
    if (!ok) pFails++;
    Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {eager.Count,4} entries");
}
Console.WriteLine($"--- PFTXS: {pfiles.Length} files, {pEntries} entries, {pMism} mismatched ---");

// ── SBP ──────────────────────────────────────────────────────────────────────
var sfiles = Directory.GetFiles(pdir, "*.sbp", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
int sEntries = 0, sMism = 0, sFails = 0;
foreach (var path in sfiles)
{
    var eager = new List<(string mag, byte[] data)>();
    using (var fs = File.OpenRead(path))
    {
        var sf = new SbpFile();
        try { sf.Read(fs); } catch { Console.WriteLine($"SKIP  {Path.GetFileName(path)}"); continue; }
        foreach (var e in sf.Entries) eager.Add((e.Magic, e.Data));
    }

    var lazy = new List<(string mag, byte[] data)>();
    using (var fs = File.OpenRead(path))
    {
        foreach (var en in LazySbpReader.Read(fs))
        {
            fs.Position = en.DataOffset;
            var raw = new byte[en.DataSize];
            int n = 0; while (n < raw.Length) { int r = fs.Read(raw, n, raw.Length - n); if (r == 0) break; n += r; }
            lazy.Add((en.Magic, raw));
        }
    }

    bool ok = eager.Count == lazy.Count;
    int lim = Math.Min(eager.Count, lazy.Count);
    for (int i = 0; i < lim; i++)
    {
        sEntries++;
        if (eager[i].mag != lazy[i].mag || !eager[i].data.AsSpan().SequenceEqual(lazy[i].data))
        { ok = false; sMism++; Console.WriteLine($"  DIFF [{i}] {eager[i].mag} ({eager[i].data.Length} vs {lazy[i].data.Length})"); }
    }
    if (!ok) sFails++;
    Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {eager.Count,4} entries");
}
Console.WriteLine($"--- SBP: {sfiles.Length} files, {sEntries} entries, {sMism} mismatched ---");

// region reader
static byte[] Region(FileStream fs, long off, int size)
{
    fs.Position = off; var b = new byte[size];
    int n = 0; while (n < b.Length) { int r = fs.Read(b, n, b.Length - n); if (r == 0) break; n += r; }
    return b;
}

// ── STP ──────────────────────────────────────────────────────────────────────
int stpFails = StpCheck(pdir);
// ── SAB ──────────────────────────────────────────────────────────────────────
int sabFails = SabCheck(pdir);
// ── FSOP ─────────────────────────────────────────────────────────────────────
int fsopFails = FsopCheck(pdir);
// ── MTAR ─────────────────────────────────────────────────────────────────────
int mtarFails = MtarCheck(pdir);

int StpCheck(string root)
{
    var files = Directory.GetFiles(root, "*.stp", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
    int ents = 0, fails = 0;
    foreach (var path in files)
    {
        var eager = new List<byte[]>();
        var sp = new StreamedPackage();
        using (var fs = File.OpenRead(path)) { try { sp.Read(fs); } catch { Console.WriteLine($"SKIP {Path.GetFileName(path)}"); continue; } }
        foreach (var e in sp.Entries) { eager.Add(e.Wem); if (e.Ls2.Length > 0 || sp.Version == StpVersion.TPP) eager.Add(e.Ls2); }

        var lazy = new List<byte[]>();
        using (var fs = File.OpenRead(path))
            foreach (var en in LazyStpReader.Read(fs))
            { lazy.Add(Region(fs, en.WemOffset, en.WemSize)); if (en.Ls2Size >= 0) lazy.Add(Region(fs, en.Ls2Offset, en.Ls2Size)); }

        // eager/lazy add wem then ls2 in opposite interleave; normalise by sorting indices is wrong —
        // instead rebuild eager in the SAME (wem, ls2) order the lazy uses:
        var eo = new List<byte[]>();
        foreach (var e in sp.Entries) { eo.Add(e.Wem); if (sp.Version == StpVersion.TPP) eo.Add(e.Ls2); }
        bool ok = eo.Count == lazy.Count;
        for (int i = 0; i < Math.Min(eo.Count, lazy.Count); i++) { ents++; if (!eo[i].AsSpan().SequenceEqual(lazy[i])) { ok = false; fails++; Console.WriteLine($"  STP DIFF {Path.GetFileName(path)} [{i}]"); } }
        Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {sp.Entries.Count,4} entries");
    }
    Console.WriteLine($"--- STP: {files.Length} files, {ents} blobs, {fails} mismatched ---");
    return fails;
}

int SabCheck(string root)
{
    var files = Directory.GetFiles(root, "*.sab", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
    int ents = 0, fails = 0;
    foreach (var path in files)
    {
        var sa = new StreamedAnimation();
        using (var fs = File.OpenRead(path)) { try { sa.Read(fs); } catch { Console.WriteLine($"SKIP {Path.GetFileName(path)}"); continue; } }
        var lazy = new List<byte[]>();
        using (var fs = File.OpenRead(path)) foreach (var en in LazySabReader.Read(fs)) lazy.Add(Region(fs, en.Offset, en.Size));
        bool ok = sa.Entries.Count == lazy.Count;
        for (int i = 0; i < Math.Min(sa.Entries.Count, lazy.Count); i++) { ents++; if (!sa.Entries[i].Lsst.AsSpan().SequenceEqual(lazy[i])) { ok = false; fails++; Console.WriteLine($"  SAB DIFF {Path.GetFileName(path)} [{i}]"); } }
        Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {sa.Entries.Count,4} entries");
    }
    Console.WriteLine($"--- SAB: {files.Length} files, {ents} blobs, {fails} mismatched ---");
    return fails;
}

int FsopCheck(string root)
{
    var files = Directory.GetFiles(root, "*.fsop", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
    int ents = 0, fails = 0;
    foreach (var path in files)
    {
        FsopFile ff; using (var fs = File.OpenRead(path)) ff = FsopFile.Read(fs);
        var lazy = new List<byte[]>();
        using (var fs = File.OpenRead(path))
            foreach (var en in LazyFsopReader.Read(fs))
            { lazy.Add(Xor(Region(fs, en.VsOffset, en.VsSize))); lazy.Add(Xor(Region(fs, en.PsOffset, en.PsSize))); }
        var eo = new List<byte[]>(); foreach (var s in ff.Shaders) { eo.Add(s.Vs); eo.Add(s.Ps); }
        bool ok = eo.Count == lazy.Count;
        for (int i = 0; i < Math.Min(eo.Count, lazy.Count); i++) { ents++; if (!eo[i].AsSpan().SequenceEqual(lazy[i])) { ok = false; fails++; Console.WriteLine($"  FSOP DIFF {Path.GetFileName(path)} [{i}]"); } }
        Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {ff.Shaders.Count,4} shaders");
    }
    Console.WriteLine($"--- FSOP: {files.Length} files, {ents} blobs, {fails} mismatched ---");
    return fails;
    static byte[] Xor(byte[] b) { for (int i = 0; i < b.Length; i++) b[i] ^= 0x9C; return b; }
}

int MtarCheck(string root)
{
    var files = Directory.GetFiles(root, "*.mtar", SearchOption.AllDirectories).OrderBy(p => p).ToArray();
    int ents = 0, fails = 0;
    foreach (var path in files)
    {
        var bytes = File.ReadAllBytes(path);
        List<MtarItem> eager; try { eager = MtarBrowse.Read(bytes); } catch { Console.WriteLine($"SKIP {Path.GetFileName(path)}"); continue; }
        var lazy = new List<(string name, byte[] data)>();
        using (var fs = File.OpenRead(path))
            foreach (var it in LazyMtarReader.Read(fs))
                lazy.Add((it.Name, it.Eager ?? Region(fs, it.Offset, it.Size)));
        bool ok = eager.Count == lazy.Count;
        for (int i = 0; i < Math.Min(eager.Count, lazy.Count); i++)
        { ents++; if (eager[i].Name != lazy[i].name || !eager[i].Data.AsSpan().SequenceEqual(lazy[i].data)) { ok = false; fails++; Console.WriteLine($"  MTAR DIFF {Path.GetFileName(path)} [{i}] {eager[i].Name} vs {lazy[i].name}"); } }
        Console.WriteLine($"{(ok ? "OK  " : "FAIL")}  {Path.GetFileName(path),-28} {eager.Count,4} items");
    }
    Console.WriteLine($"--- MTAR: {files.Length} files, {ents} items, {fails} mismatched ---");
    return fails;
}

Console.WriteLine();
int nativeFails = NativeCheck.Run();

Console.WriteLine();
int allFails = fileFails + pFails + sFails + stpFails + sabFails + fsopFails + mtarFails + nativeFails;
Console.WriteLine($"=== TOTAL {allFails} failure(s) ===");
return allFails == 0 ? 0 : 1;
