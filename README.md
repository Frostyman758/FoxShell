# Fox_shellext

A Windows Explorer **namespace shell extension** that lets you browse MGSV Fox
Engine archives (`.dat` / `.qar`, `.fpk` / `.fpkd`) as if they were folders —
drill in, preview, extract, and drag files out, including **archives nested
inside other archives** (an `.fpk` inside a `.dat`) without spilling anything to
disk. Think 7-Zip's in-Explorer browsing, but for Fox Engine formats.

## Why this project is split into two languages

Managed (.NET) shell extensions are **officially unsupported** by Microsoft:
`explorer.exe` can only host one CLR version process-wide, so a .NET in-proc
extension can be evicted by — or evict — any other managed extension on the
machine. So the in-process COM object must be **native C++**.

But all our archive-format knowledge (QAR/FPK parsing + the game's crypto and
path-hashing) already lives in the C# `Fox_parser` libraries, and I am not redoing these tools a second time. 

So the two halves talk over a tiny **C ABI**:

```
  explorer.exe
      │  COM (IShellFolder, IEnumIDList, IDataObject, IExtractIconW, …)
      ▼
  shellext/      foxshellext.dll   ── native C++ COM, NO .NET, NO Fox_parser
      │  flat C ABI (include/foxarchive.h)
      ▼
  bridge/        foxarchive.dll    ── C# compiled with NativeAOT (no CLR at runtime)
      │  #include / <Compile Include> of the AOT-clean subset
      ▼
  ../Fox_parser  QAR / FPK / GameHashing  ── My internal soon to be released tooling
```

`foxarchive.dll` is produced by **NativeAOT** (`<NativeLib>Shared</NativeLib>`),
so it is a self-contained native DLL with no CLR dependency — safe to load into
`explorer.exe`.

## External Tools (not present here)

Everything related to my  `Fox_parser` tool is confined to **one file**:
[`bridge/FoxParser.props`](bridge/FoxParser.props). It declares where
`Fox_parser` lives and exactly which `.cs` files get compiled into the bridge.

- The C++ shell extension (`shellext/`) never references `Fox_parser` — it only
  ever sees `include/foxarchive.h`.
- If `Fox_parser` moves, gets renamed, or reorganises a source file, edit
  `bridge/FoxParser.props` and nothing else.

The bridge **only uses the needed files** rather than `ProjectReference`-ing the
`Fox_parser` projects, because those projects back-reference `MgsvModBldr.Core`
(the GUI for my Mod Builder) through their JSON packer / ModBuilder code. The bridge pulls in
only the pure binary-parsing + crypto path, keeping the AOT image Core-free.

## Layout

| Path           | What                                                              |
| -------------- | ---------------------------------------------------------------- |
| `bridge/`      | NativeAOT C# → `foxarchive.dll` (+ hand-written `foxarchive.h`)   |
| `shellext/`    | C++/CMake COM namespace extension → `foxshellext.dll`             |
| `include/`     | `foxarchive.h` — the C ABI the two halves share                  |
| `installer/`   | `install.ps1` / `uninstall.ps1` — copy DLLs + register COM/CLSID  |

## Build

Prereqs: .NET 10 SDK, Visual Studio 2026 with
the C++ workload (MSVC 14.50+, Windows SDK 10.0.22621), CMake (bundled with VS).

```powershell
# 1. Native bridge (produces bridge/bin/Release/net10.0/win-x64/publish/foxarchive.dll)
dotnet publish bridge/MgsvModBldr.Tools.NativeBridge.csproj -c Release

# 2. C++ shell extension (CMake) — see shellext/ (added in a later task)
# 3. Install                       — see installer/ (added in a later task)
```

## Status

**rel1.** Browsing, per-format Type names, `.ftex` thumbnails, drag-out / copy /
paste extraction, and full Ground Zeroes (`.g0s`) support are all working.

## Supported inputs

TPP **and** Ground Zeroes:

- `.dat` / `.qar` — QAR archives (TPP)
- `.g0s` — QAR archives (Ground Zeroes; footer-detected, so the WMV `data_00.g0s`
  is correctly ignored)
- `.fpk` / `.fpkd` — Fox Packages (TPP `win` and GZ `ste` builds; GZ entry names
  resolved via `fpk_dictionary.txt`)
- `.pftxs` — Packed Fox Textures (TPP and the different GZ `PFTX`/`PSUB` layout)

…including these archives **nested inside** a `.dat`/`.g0s` (e.g. an `.fpk`,
`.fpkd`, or `.pftxs` inside a Ground Zeroes `.g0s`). Detection is by content
(magic / footer), so a generic non-Fox `.dat` is left alone.

The GZ readers live in their own files (`Fpk/Gz/`, `Pftxs/Gz/`) so the proven,
byte-exact TPP readers are never touched.

## Notes / limitations

- **Search:** Explorer filters the *current* folder's listing, but does not
  recursively search a virtual namespace (a Win11 limitation — recursive search
  needs the index or a real filesystem tree). Recursive in-archive search is a
  possible future addition via a custom results window.
- Files are extracted to `%TEMP%\fox\<archive>\…` only when you open or drag one
  out; browsing keeps everything in memory.

## Install / uninstall

```powershell
pwsh -File installer/install.ps1     # per-user, no elevation; installs to %USERPROFILE%\foxshell
pwsh -File installer/uninstall.ps1
```
