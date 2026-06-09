using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Sbp;
using MgsvModBldr.Tools.NativeBridge;   // Lazy{Fpk,Pftxs,Sbp}Reader (compiled in)

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

Console.WriteLine();
int nativeFails = NativeCheck.Run();

Console.WriteLine();
int allFails = fileFails + pFails + sFails + nativeFails;
Console.WriteLine($"=== TOTAL {allFails} failure(s) ===");
return allFails == 0 ? 0 : 1;
