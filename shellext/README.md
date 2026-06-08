# shellext/

C++/CMake COM **namespace shell extension** (`foxshellext.dll`).

This half is pure native code. It implements the COM interfaces Explorer needs
(`IShellFolder` / `IShellFolder2`, `IEnumIDList`, `IPersistFolder3`,
`IDataObject`, `IExtractIconW`, `IQueryInfo`, …) and reads archive contents
**only** through the flat C ABI in [`../include/foxarchive.h`](../include/foxarchive.h),
backed by `foxarchive.dll`.

It must **never** reference `Fox_parser` or anything .NET directly — that
coupling lives entirely in `../bridge/FoxParser.props`.

Implementation lands in task #6 (IShellFolder/IEnumIDList), #7 (drag-out),
#8 (icons/tooltips).
