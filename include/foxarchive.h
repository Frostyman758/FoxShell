/* ============================================================================
 * foxarchive.h — flat C ABI exported by foxarchive.dll (NativeAOT bridge).
 *
 * The C++ shell extension talks to the managed Fox_parser archive logic ONLY
 * through this header. Hand-written; keep in lockstep with bridge/Exports.cs.
 *
 * Conventions:
 *   - All strings are UTF-16 (wchar_t on Windows == 16-bit).
 *   - Functions return FOXARC_OK (0) on success or a negative FOXARC_E_* code.
 *   - Handles are opaque. Free archives with foxarc_close, listings with
 *     foxarc_list_free, blobs with foxarc_free_blob.
 *   - Interior paths use '/' separators, no leading slash. "" means root.
 *
 * Lifetime:
 *   - A nested archive (foxarc_open_nested) is independent of its parent once
 *     opened — it copies the inner bytes out — so the parent may be closed
 *     first. (Simpler than a borrow; inner archives are small.)
 *   - FoxItemInfo.name pointers are owned by their FoxList and become invalid
 *     after foxarc_list_free.
 * ========================================================================== */
#ifndef FOXARCHIVE_H
#define FOXARCHIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void FoxArchive;   /* opaque: an opened QAR(.dat) or FPK archive   */
typedef void FoxList;      /* opaque: a snapshot of one directory's children */

/* Return codes. */
#define FOXARC_OK            0
#define FOXARC_E_BADARG    (-1)   /* null/invalid argument                    */
#define FOXARC_E_NOTFOUND  (-2)   /* path / entry / directory does not exist  */
#define FOXARC_E_FORMAT    (-3)   /* not a recognised QAR/FPK, or corrupt     */
#define FOXARC_E_IO        (-4)   /* file open / read failure                 */
#define FOXARC_E_NOTARCH   (-5)   /* open_nested target isn't a nested archive*/
#define FOXARC_E_INTERNAL  (-99)  /* unexpected managed exception             */

/* One child of a directory (a subfolder, a file, or a nested archive). */
typedef struct FoxItemInfo {
    const wchar_t* name;       /* leaf name only; owned by the FoxList        */
    int32_t        isFolder;   /* 1 = directory (has children)                */
    int32_t        isArchive;  /* 1 = a nested QAR/FPK that can be drilled in */
    uint64_t       size;       /* uncompressed size in bytes (0 for folders)  */
    uint64_t       pathHash;   /* QAR path hash, or 0 (for tooltips)          */
} FoxItemInfo;

/* ABI version of this DLL. Bump when the struct/function shapes change. */
int32_t foxarc_abi_version(void);

/* Tell the bridge where its sidecar files (qar_dictionary.txt) live. Pass
 * foxarchive.dll's own install directory; call once after loading. Without it,
 * QAR entries fall back to hash-named paths (still browsable). NULL clears it. */
void foxarc_set_dict_dir(const wchar_t* dir);

/* Open a top-level archive from a Windows filesystem path.
 * On success *out receives a handle; close it with foxarc_close. */
int32_t foxarc_open(const wchar_t* path, FoxArchive** out);

/* Open an archive that lives *inside* another archive, by its interior path
 * (e.g. L"pack/foo.fpk"). The inner bytes are extracted in-memory; nothing is
 * written to disk. The returned handle is independent of `parent`. */
int32_t foxarc_open_nested(FoxArchive* parent, const wchar_t* interiorPath,
                           FoxArchive** out);

/* Close an archive handle (top-level or nested). NULL is ignored. */
void foxarc_close(FoxArchive* archive);

/* List the immediate children of an interior directory. dirPath="" => root.
 * On success *out receives a FoxList snapshot; free with foxarc_list_free. */
int32_t foxarc_list(FoxArchive* archive, const wchar_t* dirPath, FoxList** out);

/* Number of children in a listing. */
int32_t foxarc_list_count(FoxList* list, int32_t* count);

/* Fill *info for child [index] (0-based). The name pointer is valid until
 * foxarc_list_free(list). */
int32_t foxarc_list_item(FoxList* list, int32_t index, FoxItemInfo* info);

/* Free a listing. NULL is ignored. */
void foxarc_list_free(FoxList* list);

/* Read a file entry's full, decrypted+decompressed bytes by interior path.
 * On success *data points to a freshly-allocated buffer of *size bytes; free
 * it with foxarc_free_blob. Reading a directory returns FOXARC_E_NOTFOUND. */
int32_t foxarc_read(FoxArchive* archive, const wchar_t* interiorPath,
                    uint8_t** data, int64_t* size);

/* Free a blob returned by foxarc_read. NULL is ignored. */
void foxarc_free_blob(uint8_t* data);

/* ── FTEX texture thumbnails ─────────────────────────────────────────────────
 * Decode a small single-mip DDS from a .ftex (or .ftexs) for Explorer's
 * thumbnail grid. *dds receives a freshly-allocated DDS blob of *size bytes;
 * free it with foxarc_free_blob. Nothing is written to disk — the DDS lives
 * only in memory until the caller hands it to WIC.
 *
 * A .ftex previews the whole texture (a ~256px mip); a "<stem>.<N>.ftexs"
 * previews only the mip tier stored in that sidecar. The .ftex header is always
 * required, so interiorPath may name either — the bridge finds the siblings. */

/* From an opened archive. interiorPath may cross nested-archive boundaries
 * (e.g. "pack/tex.pftxs/Assets/.../foo.ftex"); nested containers are opened
 * in-memory and disposed right after — only their bytes are read, never the
 * whole parent. */
int32_t foxarc_ftex_thumb(FoxArchive* archive, const wchar_t* interiorPath,
                          uint8_t** dds, int64_t* size);

/* From a loose .ftex/.ftexs on the real filesystem (reads its .ftexs siblings
 * from the same directory). */
int32_t foxarc_ftex_thumb_path(const wchar_t* ftexPath,
                               uint8_t** dds, int64_t* size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FOXARCHIVE_H */
