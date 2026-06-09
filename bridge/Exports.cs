using System.Runtime;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace MgsvModBldr.Tools.NativeBridge;

// Flat C ABI exported to the C++ shell extension. See include/foxarchive.h.
// Every export is wrapped in try/catch — a managed exception must never unwind
// across the native boundary into explorer.exe.
//
// This file is the ABI surface only: argument validation, handle (un)wrapping,
// and error-code mapping. The browsable model lives in ArchiveHandle.cs and the
// directory snapshot in Listing.cs.
internal static unsafe class Exports
{
    private const int OK = 0, E_BADARG = -1, E_NOTFOUND = -2, E_FORMAT = -3,
                      E_IO = -4, E_NOTARCH = -5, E_INTERNAL = -99;

    [UnmanagedCallersOnly(EntryPoint = "foxarc_abi_version")]
    public static int AbiVersion() => 1;

    // Force a full, compacting collection and hand memory back to the OS. The
    // shell calls this after the last folder for an archive is closed, so the
    // (potentially large) index/blob memory is released promptly instead of
    // lingering in the GC heap until explorer.exe restarts.
    [UnmanagedCallersOnly(EntryPoint = "foxarc_trim")]
    public static void Trim() => TrimCore();

    // Plain managed helper so other exports (Idle) can reuse it — an
    // [UnmanagedCallersOnly] method can't be called directly from C#.
    private static void TrimCore()
    {
        try
        {
            GCSettings.LargeObjectHeapCompactionMode = GCLargeObjectHeapCompactionMode.CompactOnce;
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Aggressive, blocking: true, compacting: true);
            GC.WaitForPendingFinalizers();
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Aggressive, blocking: true, compacting: true);
        }
        catch { }
    }

    // Called when NO archive is open any more: in addition to trimming, drop the
    // cached name dictionaries (qar/gzs/fpk ~ tens of MB) so a fully-closed shell
    // holds essentially nothing. They lazily reload on the next browse.
    [UnmanagedCallersOnly(EntryPoint = "foxarc_idle")]
    public static void Idle()
    {
        try { QarNameDictionary.ClearAll(); } catch { }
        TrimCore();
    }

    // Tell the bridge where its sidecar files (qar_dictionary.txt) live. Call
    // once at load with foxarchive.dll's own directory. NULL clears it.
    [UnmanagedCallersOnly(EntryPoint = "foxarc_set_dict_dir")]
    public static void SetDictDir(char* dir)
    {
        try { QarNameDictionary.SetDir(dir == null ? null : new string(dir)); }
        catch { }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_open")]
    public static int Open(char* path, IntPtr* outHandle)
    {
        if (path == null || outHandle == null) return E_BADARG;
        *outHandle = IntPtr.Zero;
        try
        {
            var p = new string(path);
            if (!File.Exists(p)) return E_NOTFOUND;
            var h = ArchiveHandle.OpenPath(p);
            *outHandle = GCHandle.ToIntPtr(GCHandle.Alloc(h));
            return OK;
        }
        catch (InvalidDataException) { return E_FORMAT; }
        catch (IOException)          { return E_IO; }
        catch                        { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_open_nested")]
    public static int OpenNested(IntPtr parent, char* interiorPath, IntPtr* outHandle)
    {
        if (interiorPath == null || outHandle == null) return E_BADARG;
        *outHandle = IntPtr.Zero;
        try
        {
            var pa = Target<ArchiveHandle>(parent);
            if (pa is null) return E_BADARG;
            var node = pa.FindFile(new string(interiorPath));
            if (node is null) return E_NOTFOUND;
            if (!node.IsArchive) return E_NOTARCH;
            var bytes = pa.ReadFile(node);
            var h = ArchiveHandle.OpenNestedBytes(bytes, node.Name);
            *outHandle = GCHandle.ToIntPtr(GCHandle.Alloc(h));
            return OK;
        }
        catch (InvalidDataException) { return E_FORMAT; }
        catch                        { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_close")]
    public static void Close(IntPtr handle)
    {
        if (handle == IntPtr.Zero) return;
        try
        {
            var gch = GCHandle.FromIntPtr(handle);
            (gch.Target as ArchiveHandle)?.Dispose();
            gch.Free();
        }
        catch { /* swallow */ }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_list")]
    public static int List(IntPtr handle, char* dirPath, IntPtr* outList)
    {
        if (outList == null) return E_BADARG;
        *outList = IntPtr.Zero;
        try
        {
            var ar = Target<ArchiveHandle>(handle);
            if (ar is null) return E_BADARG;
            var dir = ar.NavigateDir(dirPath == null ? "" : new string(dirPath));
            if (dir is null) return E_NOTFOUND;

            var listing = new Listing(dir);
            *outList = GCHandle.ToIntPtr(GCHandle.Alloc(listing));
            return OK;
        }
        catch { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_list_count")]
    public static int ListCount(IntPtr list, int* count)
    {
        if (count == null) return E_BADARG;
        try
        {
            var l = Target<Listing>(list);
            if (l is null) return E_BADARG;
            *count = l.Count;
            return OK;
        }
        catch { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_list_item")]
    public static int ListItem(IntPtr list, int index, FoxItemInfo* info)
    {
        if (info == null) return E_BADARG;
        try
        {
            var l = Target<Listing>(list);
            if (l is null) return E_BADARG;
            if (index < 0 || index >= l.Count) return E_NOTFOUND;
            l.Fill(index, info);
            return OK;
        }
        catch { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_list_free")]
    public static void ListFree(IntPtr list)
    {
        if (list == IntPtr.Zero) return;
        try
        {
            var gch = GCHandle.FromIntPtr(list);
            (gch.Target as Listing)?.Dispose();
            gch.Free();
        }
        catch { }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_read")]
    public static int Read(IntPtr handle, char* interiorPath, byte** data, long* size)
    {
        if (data == null || size == null) return E_BADARG;
        *data = null; *size = 0;
        try
        {
            var ar = Target<ArchiveHandle>(handle);
            if (ar is null || interiorPath == null) return E_BADARG;
            var node = ar.FindFile(new string(interiorPath));
            if (node is null) return E_NOTFOUND;
            var bytes = ar.ReadFile(node);

            var buf = (byte*)NativeMemory.Alloc((nuint)bytes.Length);
            if (bytes.Length > 0)
                fixed (byte* src = bytes) Unsafe.CopyBlock(buf, src, (uint)bytes.Length);
            *data = buf;
            *size = bytes.Length;
            return OK;
        }
        catch (InvalidDataException) { return E_FORMAT; }
        catch (IOException)          { return E_IO; }
        catch                        { return E_INTERNAL; }
    }

    // Build a thumbnail-sized DDS for a .ftex at interiorPath (reads its
    // .ftexs sidecars from the same archive). On success *data is a freshly
    // allocated DDS buffer of *size bytes; free with foxarc_free_blob.
    [UnmanagedCallersOnly(EntryPoint = "foxarc_ftex_thumb")]
    public static int FtexThumb(IntPtr handle, char* interiorPath, byte** data, long* size)
    {
        if (data == null || size == null) return E_BADARG;
        *data = null; *size = 0;
        try
        {
            var ar = Target<ArchiveHandle>(handle);
            if (ar is null || interiorPath == null) return E_BADARG;
            var dds = FtexThumbnail.Build(ar, new string(interiorPath));
            if (dds is null) return E_FORMAT;

            var buf = (byte*)NativeMemory.Alloc((nuint)dds.Length);
            fixed (byte* src = dds) Unsafe.CopyBlock(buf, src, (uint)dds.Length);
            *data = buf;
            *size = dds.Length;
            return OK;
        }
        catch (InvalidDataException) { return E_FORMAT; }
        catch (IOException)          { return E_IO; }
        catch                        { return E_INTERNAL; }
    }

    // Same, but for a loose .ftex on disk (reads its .ftexs siblings from the
    // same directory). Used for real-filesystem .ftex thumbnails and testing.
    [UnmanagedCallersOnly(EntryPoint = "foxarc_ftex_thumb_path")]
    public static int FtexThumbPath(char* ftexPath, byte** data, long* size)
    {
        if (data == null || size == null) return E_BADARG;
        *data = null; *size = 0;
        try
        {
            if (ftexPath == null) return E_BADARG;
            var dds = FtexThumbnail.BuildFromFiles(new string(ftexPath));
            if (dds is null) return E_FORMAT;
            var buf = (byte*)NativeMemory.Alloc((nuint)dds.Length);
            fixed (byte* src = dds) Unsafe.CopyBlock(buf, src, (uint)dds.Length);
            *data = buf; *size = dds.Length;
            return OK;
        }
        catch (InvalidDataException) { return E_FORMAT; }
        catch (IOException)          { return E_IO; }
        catch                        { return E_INTERNAL; }
    }

    [UnmanagedCallersOnly(EntryPoint = "foxarc_free_blob")]
    public static void FreeBlob(byte* data)
    {
        if (data != null) NativeMemory.Free(data);
    }

    private static T? Target<T>(IntPtr h) where T : class
        => h == IntPtr.Zero ? null : GCHandle.FromIntPtr(h).Target as T;
}
