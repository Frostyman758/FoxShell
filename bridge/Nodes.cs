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

    // Set per owning-archive kind for the formats still read eagerly (QAR + G0s
    // decrypt per entry; the GZ readers are nested-only). Lazy formats use Lazy
    // instead (see below); fpk/pftxs/sbp/stp/sab/fsop/mtar are all lazy now.
    public QarEntry?     Qar;
    public GzFpkEntry?   GzFpk;
    public GzPftxsEntry? GzPftxs;
    public G0sEntry?     G0s;

    // Resolved-in-memory bytes for formats whose entries are plain blobs
    // (sbp/stp/sab — and any future flat container). ReadFile returns this
    // directly; a blob that is itself an archive (e.g. an .stp inside an .sbp)
    // is re-detected by magic when drilled into.
    public byte[]?       Blob;

    // LAZY entry: read on demand from the archive's source (file or nested
    // bytes) instead of being materialised at open time. Big archives then cost
    // only their index, and a file is decoded only when actually touched.
    public LazyBlob?     Lazy;
}

// A region of the owning archive that holds one file's bytes, decoded on read.
internal sealed class LazyBlob
{
    public long   Offset;
    public int    Length;
    public byte   Decode;     // 0 = raw, 1 = fpk crypto (Key = entry path), 2 = xor 0x9C
    public string Key = "";

    public const byte Raw = 0, Fpk = 1, Xor9C = 2;
}
