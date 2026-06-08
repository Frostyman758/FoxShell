/* ============================================================================
 * foxtypes.h — extension -> friendly "Type" name, from the MGSV Modding Wiki
 * "File Formats" page (the Full Name column).
 *
 * The shell extension uses this to fill Explorer's Type column per item, so a
 * .fpk reads "Fox Package", a .fpkd "Fox Package Data", a .dat "QAR Archive",
 * etc. — instead of the single registered ProgID name ("MGSV Fox Archive") that
 * the shell would otherwise stamp on every associated item.
 *
 * Names follow the wiki's Full Name column, cleaned up: where the wiki lists
 * alternatives ("Fox Package / fox::PackFile") we take the first, and trailing
 * "(fox2)"/"(fox)" qualifiers are dropped. Unknown extensions fall back to the
 * standard "<EXT> File" convention, so nothing ever shows the wrong name.
 * ========================================================================== */
#ifndef FOXTYPES_H
#define FOXTYPES_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cwctype>

struct FoxTypeEntry { const wchar_t* ext; const wchar_t* name; };

// Sorted-by-frequency-is-irrelevant; lookup is a small linear scan. Keep keys
// lowercase, no leading dot.
static const FoxTypeEntry kFoxTypeTable[] = {
    // ── Containers / archives ────────────────────────────────────────────
    { L"dat",    L"QAR Archive" },
    { L"qar",    L"QAR Archive" },
    { L"g0s",    L"QAR Archive (GZ)" },
    { L"fpk",    L"Fox Package" },
    { L"fpkd",   L"Fox Package Data" },
    { L"fpkl",   L"Fox Package Link" },
    { L"pftxs",  L"Packed Fox Textures" },
    { L"sbp",    L"Sound Bank Package" },
    { L"stp",    L"Streamed Package" },
    { L"sab",    L"Sound Additional Binaries" },
    { L"mtar",   L"Motion Archive" },
    { L"mtard",  L"Motion Archive Data" },
    { L"fsm",    L"Fox Stream Movie" },
    // ── Textures / models / shaders ──────────────────────────────────────
    { L"ftex",   L"Fox Texture" },
    { L"ftexs",  L"Fox Sub-texture" },
    { L"fmdl",   L"FmdlFile" },
    { L"fmtt",   L"Material Parameters" },
    { L"fsop",   L"Fox Shader Object Pack" },
    { L"fv2",    L"FormVariationFile2" },
    { L"fova",   L"Form Variation" },
    { L"dfrm",   L"Deformation" },
    { L"frdv",   L"HelpBoneFile" },
    { L"frig",   L"RigFile" },
    { L"ftdp",   L"FoxTerrainDecalPackFile" },
    // ── Animation ────────────────────────────────────────────────────────
    { L"gani",   L"AnimFile" },
    { L"cani",   L"Camera Animation" },
    { L"caar",   L"Camera Animation Archive" },
    { L"lani",   L"LaniFile" },
    { L"sani",   L"Stream Animation" },
    { L"sand",   L"SandFile" },
    { L"mog",    L"MotionGraphFile" },
    { L"trk",    L"Animation Track" },
    { L"chnk",   L"Mtar Chunk" },
    { L"enchnk", L"Mtar Animation Chunk" },
    { L"exchnk", L"Mtar Extra Chunk" },
    // ── Data sets / fox2 family ──────────────────────────────────────────
    { L"fox2",   L"DataSetFile2" },
    { L"fox",    L"Fox XML" },
    { L"las",    L"DataSet" },
    { L"parts",  L"Model Description Data" },
    { L"tgt",    L"Target" },
    { L"veh",    L"Vehicle" },
    { L"vdp",    L"Vehicle Driving Parameter" },
    // ── Physics / collision / destruction ────────────────────────────────
    { L"ph",     L"Physics" },
    { L"phsd",   L"Physics Sound Data" },
    { L"sim",    L"SimBinaryFile" },
    { L"clo",    L"Cloth Configuration File" },
    { L"fclo",   L"Cloth Settings" },
    { L"des",    L"Destruction Data" },
    { L"fdes",   L"FoxDestruction" },
    { L"geom",   L"GeoGeomFile" },
    { L"geoms",  L"GeoxGeomSetFile" },
    { L"geobv",  L"GeoBoundingVolumeFile" },
    { L"gskl",   L"GeoGsklFile" },
    { L"gpfp",   L"GeoPathFixedPackFile" },
    { L"trap",   L"GeoTrapFile" },
    { L"bnd",    L"Bounder" },
    // ── Navigation / AI ──────────────────────────────────────────────────
    { L"nav2",   L"NavNavigationFile" },
    { L"nta",    L"NavTacticalAction" },
    { L"frt",    L"TppRouteSet" },
    { L"frl",    L"RailFile" },
    { L"frld",   L"RailUniqueIdFile" },
    { L"tcvp",   L"CoverPointFile" },
    { L"aib",    L"AI Behavior" },
    { L"aibc",   L"AI Behavior Category" },
    // ── Lighting / terrain / world ───────────────────────────────────────
    { L"grxla",  L"LightArrayFile" },
    { L"grxoc",  L"OccluderArrayFile" },
    { L"lpsh",   L"Light Probe Spherical Harmonics" },
    { L"atsh",   L"Atmosphere Spherical Harmonics" },
    { L"pcsp",   L"PrecomputeSkyFile" },
    { L"htre",   L"TerrainTileFile" },
    { L"tre2",   L"TerrainFile" },
    { L"fstb",   L"StageBlockFile" },
    { L"obr",    L"Object Brush" },
    { L"obrb",   L"Object Brush Block" },
    { L"lba",    L"Locator Binary Array" },
    { L"fcnp",   L"ConnectPointFile" },
    // ── Audio / speech / text ────────────────────────────────────────────
    { L"bnk",    L"Wwise SoundBank" },
    { L"wem",    L"Wwise Encoded Media" },
    { L"sdf",    L"SoundDataFile" },
    { L"spch",   L"TppSpeechFile" },
    { L"rdf",    L"Radio Data File" },
    { L"lng",    L"LangFile" },
    { L"lng2",   L"LangFile2" },
    { L"subp",   L"SubtitlesPackageFile" },
    { L"lad",    L"Lip Adjust Data" },
    { L"ladb",   L"LadbFile" },
    { L"ls",     L"Lip Sync" },
    { L"st",     L"SubTitle" },
    { L"evf",    L"EvfFile" },
    // ── UI / fonts ───────────────────────────────────────────────────────
    { L"uia",    L"UiAnimFile" },
    { L"uif",    L"UiFile" },
    { L"uigb",   L"UiGraphFile" },
    { L"uilb",   L"UiLayoutFile" },
    { L"uig",    L"UI Graph" },
    { L"uil",    L"UI Layout" },
    { L"mbl",    L"Mother Base Layout" },
    { L"ends",   L"EndingSettingsFile" },
    { L"fnt",    L"Font" },
    { L"ffnt",   L"Fox Font" },
    // ── Effects / misc ───────────────────────────────────────────────────
    { L"vfx",    L"FxVfxFile" },
    { L"vfxlf",  L"VFX Lens Flare" },
    { L"vfxlb",  L"VFX Locator Array" },
    { L"vfxdb",  L"VFX Database" },
    { L"fsd",    L"Facial Setting Data" },
    { L"twpf",   L"ParametersFile" },
    { L"fxp",    L"Fox Project" },
    { L"fcnpx",  L"Extended Connect Point File" },
    // ── Plain text / data ────────────────────────────────────────────────
    { L"lua",    L"Lua" },
    { L"json",   L"JSON" },
    { L"xml",    L"XML" },
};

// Lowercase a short extension into a fixed buffer (ASCII-only is fine here).
inline void FoxLowerExt(const wchar_t* ext, wchar_t* out, size_t cap)
{
    size_t i = 0;
    for (; ext[i] && i + 1 < cap; i++) out[i] = (wchar_t)towlower(ext[i]);
    out[i] = 0;
}

// Fill `out` with the friendly Type string for an item. Always succeeds:
//   - folders            -> "File folder"
//   - known extension    -> the wiki Full Name (e.g. ".fpk" -> "Fox Package")
//   - unknown w/ ext      -> "<EXT> File"      (e.g. ".abc" -> "ABC File")
//   - archive, no ext     -> "Fox Archive"
//   - otherwise           -> "File"
inline void FoxFriendlyType(const wchar_t* leafName, bool isFolder, bool isArchive,
                            wchar_t* out, size_t cap)
{
    if (isFolder) { lstrcpynW(out, L"File folder", (int)cap); return; }

    const wchar_t* dot = wcsrchr(leafName, L'.');
    if (dot && dot[1])
    {
        wchar_t ext[24];
        FoxLowerExt(dot + 1, ext, 24);
        for (const auto& e : kFoxTypeTable)
            if (wcscmp(ext, e.ext) == 0) { lstrcpynW(out, e.name, (int)cap); return; }

        // Unknown extension: "<EXT> File", extension upper-cased like Explorer.
        wchar_t up[24];
        size_t i = 0;
        for (; dot[1 + i] && i + 1 < 24; i++) up[i] = (wchar_t)towupper(dot[1 + i]);
        up[i] = 0;
        _snwprintf_s(out, cap, _TRUNCATE, L"%s File", up);
        return;
    }

    lstrcpynW(out, isArchive ? L"Fox Archive" : L"File", (int)cap);
}

#endif /* FOXTYPES_H */
