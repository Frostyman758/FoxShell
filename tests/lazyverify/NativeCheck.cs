using System.Runtime.InteropServices;
using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Sbp;

// End-to-end check against the ACTUAL shipped NativeAOT binary (dist\foxarchive.dll):
// open each fixture through the C ABI, recursively read every file via foxarc_read,
// and compare the bytes to the eager Fox_parser oracle. This proves the lazy path
// works inside the real DLL the shell loads, not just in the source we compiled in.
internal static class NativeCheck
{
    const string DLL = @"C:\rsearch\Fox_shellext\dist\foxarchive.dll";

    [StructLayout(LayoutKind.Sequential)]
    struct FoxItemInfo { public IntPtr name; public int isFolder; public int isArchive; public ulong size; public ulong pathHash; }

    [DllImport(DLL)] static extern int  foxarc_open([MarshalAs(UnmanagedType.LPWStr)] string path, out IntPtr arc);
    [DllImport(DLL)] static extern int  foxarc_list(IntPtr arc, [MarshalAs(UnmanagedType.LPWStr)] string dir, out IntPtr list);
    [DllImport(DLL)] static extern int  foxarc_list_count(IntPtr list, out int count);
    [DllImport(DLL)] static extern int  foxarc_list_item(IntPtr list, int idx, out FoxItemInfo info);
    [DllImport(DLL)] static extern void foxarc_list_free(IntPtr list);
    [DllImport(DLL)] static extern int  foxarc_read(IntPtr arc, [MarshalAs(UnmanagedType.LPWStr)] string path, out IntPtr data, out long size);
    [DllImport(DLL)] static extern void foxarc_free_blob(IntPtr data);
    [DllImport(DLL)] static extern void foxarc_close(IntPtr arc);
    [DllImport(DLL)] static extern void foxarc_set_dict_dir([MarshalAs(UnmanagedType.LPWStr)] string dir);

    static void WalkFiles(IntPtr arc, string dir, List<string> outFiles)
    {
        if (foxarc_list(arc, dir, out var list) != 0) return;
        foxarc_list_count(list, out int c);
        var subdirs = new List<string>();
        for (int i = 0; i < c; i++)
        {
            foxarc_list_item(list, i, out var info);
            var name = Marshal.PtrToStringUni(info.name)!;     // copies before list_free invalidates it
            var full = dir.Length == 0 ? name : dir + "/" + name;
            if (info.isFolder != 0) subdirs.Add(full); else outFiles.Add(full);
        }
        foxarc_list_free(list);
        foreach (var sd in subdirs) WalkFiles(arc, sd, outFiles);
    }

    static byte[] Read(IntPtr arc, string path)
    {
        if (foxarc_read(arc, path, out var data, out long size) != 0) return Array.Empty<byte>();
        var buf = new byte[size];
        if (size > 0) Marshal.Copy(data, buf, 0, (int)size);
        foxarc_free_blob(data);
        return buf;
    }

    public static int Run()
    {
        // Point the DLL at the dictionaries so pftxs entries resolve to real names
        // (the same names the eager oracle won't have — so for pftxs we compare by
        // bytes read back through the path the DLL itself reports).
        try { foxarc_set_dict_dir(@"C:\rsearch\Fox_shellext\dist"); } catch { }

        int fails = 0;
        fails += CheckFpk();
        fails += CheckSbp();
        Console.WriteLine($"--- NATIVE DLL end-to-end: {(fails == 0 ? "ALL OK" : fails + " FAILED")} ---");
        return fails;
    }

    static int CheckFpk()
    {
        int fails = 0, files = 0, entries = 0;
        foreach (var path in Directory.GetFiles(@"C:\rsearch\test_fixtures\fpk", "*.fpk")
                                      .Concat(Directory.GetFiles(@"C:\rsearch\test_fixtures\fpk", "*.fpkd")))
        {
            files++;
            var eager = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
            using (var fs = File.OpenRead(path)) { var f = new FpkFile(); f.Read(fs); foreach (var e in f.Entries) eager[e.FilePath.Data.Replace('\\','/').TrimStart('/')] = e.Data; }

            if (foxarc_open(path, out var arc) != 0) { Console.WriteLine($"  NATIVE open FAIL {Path.GetFileName(path)}"); fails++; continue; }
            var got = new List<string>(); WalkFiles(arc, "", got);
            bool ok = got.Count == eager.Count;
            foreach (var p in got)
            {
                entries++;
                var nb = Read(arc, p);
                if (!eager.TryGetValue(p, out var eb) || !eb.AsSpan().SequenceEqual(nb))
                { ok = false; fails++; Console.WriteLine($"  NATIVE DIFF {Path.GetFileName(path)} :: {p}"); }
            }
            foxarc_close(arc);
            if (!ok) Console.WriteLine($"  NATIVE FAIL {Path.GetFileName(path)}");
        }
        Console.WriteLine($"    fpk via DLL: {files} files, {entries} entries, {fails} bad");
        return fails;
    }

    static int CheckSbp()
    {
        int fails = 0, files = 0, entries = 0;
        foreach (var path in Directory.GetFiles(@"C:\rsearch\test_fixtures", "*.sbp", SearchOption.AllDirectories))
        {
            files++;
            var eager = new List<byte[]>();
            using (var fs = File.OpenRead(path)) { var s = new SbpFile(); s.Read(fs); foreach (var e in s.Entries) eager.Add(e.Data); }

            if (foxarc_open(path, out var arc) != 0) { Console.WriteLine($"  NATIVE open FAIL {Path.GetFileName(path)}"); fails++; continue; }
            var got = new List<string>(); WalkFiles(arc, "", got);
            got.Sort(StringComparer.Ordinal);   // "0.bnk","1.stp"... already index-ordered names
            bool ok = got.Count == eager.Count;
            for (int i = 0; i < Math.Min(got.Count, eager.Count); i++)
            {
                entries++;
                var nb = Read(arc, got[i]);
                if (!eager[i].AsSpan().SequenceEqual(nb)) { ok = false; fails++; Console.WriteLine($"  NATIVE DIFF {Path.GetFileName(path)} :: {got[i]}"); }
            }
            foxarc_close(arc);
            if (!ok) Console.WriteLine($"  NATIVE FAIL {Path.GetFileName(path)}");
        }
        Console.WriteLine($"    sbp via DLL: {files} files, {entries} entries, {fails} bad");
        return fails;
    }
}
