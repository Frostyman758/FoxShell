# installer/

`install.ps1` / `uninstall.ps1` — copy `foxarchive.dll` + `foxshellext.dll` into
`%ProgramFiles%\modbldr-shellext\`, register the COM server, wire the namespace
extension under the `.dat` / `.qar` / `.fpk` / `.fpkd` file types, and add the
CLSID to the Shell Extensions "Approved" list.

`uninstall.ps1` reverses all of the above.

Implementation lands in task #9.
