# include/

`foxarchive.h` — the flat C ABI shared by the NativeAOT bridge
(`../bridge/foxarchive.dll`) and the C++ shell extension (`../shellext/`).

NativeAOT does **not** auto-generate a C header for `[UnmanagedCallersOnly]`
exports, so `foxarchive.h` is **hand-written** and must be kept in lockstep with
`../bridge/Exports.cs`. The ABI is designed in task #3 and implemented in task #4.

`foxarchive.lib` (the import library NativeAOT emits next to the DLL) is copied
here at build time and is git-ignored.
