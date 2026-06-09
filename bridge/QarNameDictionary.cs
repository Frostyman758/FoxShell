using MgsvModBldr.Tools.Qar;
using MgsvModBldr.Tools.G0s;
using MgsvModBldr.Tools.Fpk.Gz;

namespace MgsvModBldr.Tools.NativeBridge;

// Lazy, process-wide loader for qar_dictionary.txt — the table that maps QAR
// path hashes back to readable filenames. QAR and PFTXS trees use it to label
// entries; without it they fall back to hash-named paths (still browsable).
//
// The host (shell ext / test) must point us at the directory holding the dict,
// because for a NativeAOT DLL AppContext.BaseDirectory is the *host* process
// (explorer.exe), not foxarchive.dll's own folder.
internal static class QarNameDictionary
{
    private static string? _dir;
    private static QarDictionary? _dict;
    private static bool _tried;

    // Set (or clear, with null) where the sidecar dictionaries live. Resets the
    // cache so the next Get() re-loads from the new location. The same directory
    // is handed to G0sHash for gzs_dictionary.txt — the GZ (.g0s) names — which
    // would otherwise look beside explorer.exe instead of foxarchive.dll.
    public static void SetDir(string? dir)
    {
        _dir = dir; _tried = false; _dict = null;
        G0sHash.DictionaryDirectory = dir;       // gzs_dictionary.txt (GZ .g0s names)
        FpkDictionary.DictionaryDirectory = dir; // fpk_dictionary.txt (GZ fpk names)
    }

    public static QarDictionary? Get()
    {
        if (_tried) return _dict;
        _tried = true;
        try
        {
            var path = _dir is not null
                ? Path.Combine(_dir, QarDictionary.DictionaryFileName)
                : null;                       // null => QarDictionary's default lookup
            _dict = QarDictionary.Load(path);
        }
        catch { _dict = null; }
        return _dict;
    }

    // Drop every cached name dictionary (qar/gzs/fpk) so its memory is freed when
    // no archive is open. They lazily reload on the next browse. The dir is kept,
    // so reload uses the same location. (Re-assigning DictionaryDirectory to its
    // current value is how G0sHash/FpkDictionary expose a cache reset without a
    // Fox_parser change.)
    public static void ClearAll()
    {
        _tried = false; _dict = null;                        // qar (ours)
        G0sHash.DictionaryDirectory = G0sHash.DictionaryDirectory;          // gzs
        FpkDictionary.DictionaryDirectory = FpkDictionary.DictionaryDirectory; // fpk
    }
}
