using MgsvModBldr.Tools.Qar;
using MgsvModBldr.Tools.Fpk;
using MgsvModBldr.Tools.Fpk.Gz;
using MgsvModBldr.Tools.Pftxs;
using MgsvModBldr.Tools.Pftxs.Gz;
using MgsvModBldr.Tools.G0s;
using MgsvModBldr.Tools.Sbp;
using MgsvModBldr.Tools.Stp;
using MgsvModBldr.Tools.Fsop;
using MgsvModBldr.Tools.Mtar;

namespace MgsvModBldr.Tools.NativeBridge;

// Tree construction for ArchiveHandle: turn each archive's flat entry list into
// the DirNode/FileNode hierarchy the shell walks. One BuildXxxTree per format.
internal sealed partial class ArchiveHandle
{
    private void BuildQarTree()
    {
        var dict = QarNameDictionary.Get();
        foreach (var e in _qar!.Entries)
        {
            string path;
            if (dict is not null)
            {
                path = dict.Resolve(e.Header.PathHash, out bool found);
                if (!found) path = $"_unresolved/{e.Header.PathHash:x16}";
            }
            else path = $"_unresolved/{e.Header.PathHash:x16}";

            e.Header.FilePath = path;
            // UncompressedSize includes the encryption data-header for encrypted
            // entries; subtract it so the listed size matches extracted bytes.
            ulong dh = (ulong)QarConstants.GetDataHeaderSize(e.DataHeader.EncryptionMagic);
            ulong shownSize = e.Header.UncompressedSize > dh
                ? e.Header.UncompressedSize - dh
                : e.Header.UncompressedSize;
            var leaf = AddPath(path, shownSize, e.Header.PathHash);
            leaf.Qar = e;
            leaf.IsArchive = FoxFormats.IsNestedContainer(leaf.Name);
        }
    }

    private void BuildFpkTree(List<LazyFpkReader.Entry> entries)
    {
        foreach (var e in entries)         // index only — bytes pulled on demand
        {
            var leaf = AddPath(e.Path, (ulong)e.DataSize, 0);
            leaf.Lazy = new LazyBlob { Offset = e.DataOffset, Length = e.DataSize, Decode = LazyBlob.Fpk, Key = e.Path };
            leaf.IsArchive = FoxFormats.IsNestedContainer(leaf.Name);
        }
    }

    private void BuildGzFpkTree(GzFpkFile fpk)
    {
        // Entry paths are MD5-resolved (real path from fpk_dictionary, else
        // <md5hex><ext>); see GzFpkString. Data is plaintext, already in memory.
        foreach (var e in fpk.Entries)
        {
            var leaf = AddPath(e.FilePath, (ulong)e.Data.LongLength, 0);
            leaf.GzFpk = e;
            leaf.IsArchive = FoxFormats.IsGzNestedContainer(leaf.Name);
        }
    }

    private void BuildGzPftxsTree(GzPftxsFile pftxs)
    {
        foreach (var e in pftxs.Files)   // flat .ftex + .ftexs entries
        {
            var leaf = AddPath(e.Path, (ulong)e.Data.LongLength, 0);
            leaf.GzPftxs = e;
        }
    }

    // .sbp — sub-files tagged by a 4-byte magic (bnk/stp/sab), no names. Name
    // them "<index>.<tag>"; the stp/sab ones are themselves containers (drilled
    // into by magic) while bnk is a leaf.
    private void BuildSbpTree(List<LazySbpReader.Entry> entries)
    {
        for (int i = 0; i < entries.Count; i++)   // index only — sub-files pulled on demand
        {
            var e = entries[i];
            var tag = string.IsNullOrEmpty(e.Magic) ? "bin" : e.Magic;
            var leaf = AddPath($"{i}.{tag}", (ulong)e.DataSize, 0);
            leaf.Lazy = new LazyBlob { Offset = e.DataOffset, Length = e.DataSize, Decode = LazyBlob.Raw };
            leaf.IsArchive = FoxFormats.IsNestedContainer(leaf.Name);
        }
    }

    // .stp — each entry is a hash + a .wem (and, on TPP, a .ls2 lipsync).
    private void BuildStpTree(StreamedPackage stp)
    {
        foreach (var e in stp.Entries)
        {
            var stem = e.Name.ToString("x8");
            var wem = AddPath($"{stem}.wem", (ulong)e.Wem.LongLength, e.Name);
            wem.Blob = e.Wem;
            if (e.Ls2.Length > 0)
            {
                var ls2 = AddPath($"{stem}.ls2", (ulong)e.Ls2.LongLength, e.Name);
                ls2.Blob = e.Ls2;
            }
        }
    }

    // .sab — each entry is a 64-bit hash + a combined .lsst lip-sync block.
    private void BuildSabTree(StreamedAnimation sab)
    {
        foreach (var e in sab.Entries)
        {
            var leaf = AddPath($"{e.Name:x16}.lsst", (ulong)e.Lsst.LongLength, e.Name);
            leaf.Blob = e.Lsst;
        }
    }

    // .fsop — each shader becomes "<name>_vs.fxc" + "<name>_ps.fxc" (DXBC blobs,
    // already XOR-decoded). Names can collide across entries, so prefix the index.
    private void BuildFsopTree(FsopFile fsop)
    {
        for (int i = 0; i < fsop.Shaders.Count; i++)
        {
            var s = fsop.Shaders[i];
            var stem = $"{i:0000}_{s.Name}";
            var vs = AddPath($"{stem}_vs.fxc", (ulong)s.Vs.LongLength, 0);
            vs.Blob = s.Vs;
            var ps = AddPath($"{stem}_ps.fxc", (ulong)s.Ps.LongLength, 0);
            ps.Blob = s.Ps;
        }
    }

    // .mtar — the flat gani/trk/chnk/exchnk/enchnk file set MtarBrowse extracts.
    private void BuildMtarTree(List<MtarItem> items)
    {
        foreach (var e in items)
        {
            var leaf = AddPath(e.Name, (ulong)e.Data.LongLength, 0);
            leaf.Blob = e.Data;
        }
    }

    private void BuildPftxsTree(List<LazyPftxsReader.Entry> entries)
    {
        var dict = QarNameDictionary.Get();
        foreach (var e in entries)         // index only — texture bytes pulled on demand
        {
            string path;
            if (dict is not null)
            {
                path = dict.Resolve(e.Hash, out bool found);
                if (!found) path = $"_unresolved/{e.Hash:x16}.ftex";
            }
            else path = $"_unresolved/{e.Hash:x16}.ftex";

            var leaf = AddPath(path, (ulong)e.DataSize, e.Hash);
            leaf.Lazy = new LazyBlob { Offset = e.DataOffset, Length = e.DataSize, Decode = LazyBlob.Raw };
            leaf.IsArchive = FoxFormats.IsNestedContainer(leaf.Name);
        }
    }

    private void BuildG0sTree()
    {
        // G0sHash.TryResolve always yields a path: the dictionary stem (if found)
        // or the 48-bit hash in hex, plus the GZ typeId's extension. So even
        // without gzs_dictionary.txt, entries browse with correct extensions.
        foreach (var e in _g0s!.Entries)
        {
            G0sHash.TryResolve(e.Hash, out var path);
            if (string.IsNullOrEmpty(path)) path = $"_unresolved/{e.Hash:x16}";
            // Size is the raw on-disk blob (includes the 8-byte inner header for
            // inner-encrypted entries); good enough for the listing.
            var leaf = AddPath(path, e.Size, e.Hash);
            leaf.G0s = e;
            // GZ-specific: fpk/fpkd browse, but GZ .pftxs can't be parsed by our
            // reader so it stays a plain (extractable) file.
            leaf.IsArchive = FoxFormats.IsGzNestedContainer(leaf.Name);
        }
    }

    // Insert a file at its interior path, creating intermediate folders.
    private FileNode AddPath(string rawPath, ulong size, ulong hash)
    {
        var norm = rawPath.Replace('\\', '/').TrimStart('/');
        var parts = norm.Split('/', StringSplitOptions.RemoveEmptyEntries);
        var dir = Root;
        for (int i = 0; i < parts.Length - 1; i++)
        {
            if (!dir.Dirs.TryGetValue(parts[i], out var child))
            {
                child = new DirNode();
                dir.Dirs[parts[i]] = child;
            }
            dir = child;
        }
        var leafName = parts.Length > 0 ? parts[^1] : norm;
        var node = new FileNode { Name = leafName, Size = size, Hash = hash };
        dir.Files.Add(node);
        return node;
    }
}
