using MgsvModBldr.Tools.Qar;
using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Fpk.Gz;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Pftxs.Gz;
using MgsvModBldr.Tools.G0s;

namespace MgsvModBldr.Tools.NativeBridge;

// The synthesised directory tree an ArchiveHandle exposes. An archive stores a
// flat list of entries with full interior paths; we fold those into folders so
// the shell can ask "what's in this directory?" instead of parsing paths.

internal sealed class DirNode
{
    public SortedDictionary<string, DirNode> Dirs { get; } = new(StringComparer.OrdinalIgnoreCase);
    public List<FileNode> Files { get; } = new();
}

internal sealed class FileNode
{
    public string Name = "";
    public ulong  Size;
    public ulong  Hash;
    public bool   IsArchive;     // a nested container we can drill into

    // Exactly one of these is set, depending on the owning archive's kind.
    public QarEntry?     Qar;
    public FpkEntry?     Fpk;
    public GzFpkEntry?   GzFpk;
    public PftxsEntry?   Pftxs;
    public GzPftxsEntry? GzPftxs;
    public G0sEntry?     G0s;

    // Resolved-in-memory bytes for formats whose entries are plain blobs
    // (sbp/stp/sab — and any future flat container). ReadFile returns this
    // directly; a blob that is itself an archive (e.g. an .stp inside an .sbp)
    // is re-detected by magic when drilled into.
    public byte[]?       Blob;
}
