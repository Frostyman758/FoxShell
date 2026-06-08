using System.Runtime.InteropServices;

namespace MgsvModBldr.Tools.NativeBridge;

// One child of a directory, marshalled flat to the C++ side. Layout must match
// FoxItemInfo in include/foxarchive.h exactly.
[StructLayout(LayoutKind.Sequential)]
internal struct FoxItemInfo
{
    public IntPtr name;
    public int    isFolder;
    public int    isArchive;
    public ulong  size;
    public ulong  pathHash;
}

// Snapshot of one directory's children, with native-allocated name strings kept
// alive until the listing is freed. Folders sort first, then files; both alpha.
// The shell pulls items by index via foxarc_list_count / foxarc_list_item.
internal sealed unsafe class Listing : IDisposable
{
    private readonly IntPtr[] _names;
    private readonly int[]    _isFolder;
    private readonly int[]    _isArchive;
    private readonly ulong[]  _size;
    private readonly ulong[]  _hash;
    public int Count { get; }

    public Listing(DirNode dir)
    {
        var dirs  = dir.Dirs.Keys.ToList();                 // already sorted
        var files = dir.Files.OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase).ToList();
        Count = dirs.Count + files.Count;

        _names     = new IntPtr[Count];
        _isFolder  = new int[Count];
        _isArchive = new int[Count];
        _size      = new ulong[Count];
        _hash      = new ulong[Count];

        int i = 0;
        foreach (var d in dirs)
        {
            _names[i] = Marshal.StringToHGlobalUni(d);
            _isFolder[i] = 1;
            i++;
        }
        foreach (var f in files)
        {
            _names[i]     = Marshal.StringToHGlobalUni(f.Name);
            _isArchive[i] = f.IsArchive ? 1 : 0;
            _size[i]      = f.Size;
            _hash[i]      = f.Hash;
            i++;
        }
    }

    public void Fill(int index, FoxItemInfo* info)
    {
        info->name      = _names[index];
        info->isFolder  = _isFolder[index];
        info->isArchive = _isArchive[index];
        info->size      = _size[index];
        info->pathHash  = _hash[index];
    }

    public void Dispose()
    {
        foreach (var p in _names)
            if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
    }
}
