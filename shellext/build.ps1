# Build the C++ shell-extension artifacts with MSVC.
#   - foxshellext.dll : the COM namespace extension Explorer hosts.
#   - foxbrowse.exe   : standalone "open archive in an Explorer view" launcher.
# Usage:  pwsh -File build.ps1            -> builds to .\build\
$ErrorActionPreference = "Stop"
$here   = Split-Path -Parent $MyInvocation.MyCommand.Path
$out    = Join-Path $here "build"
New-Item -ItemType Directory -Force $out | Out-Null

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

# foxshellext.dll — one .cpp per COM responsibility.
$dllSrcs = @(
    "dllmain.cpp",          # DLL entry + class factory + COM registration
    "FoxShellFolder.cpp",   # IShellFolder2 / IPersistFolder3 core
    "FoxContextMenu.cpp",   # right-click / open verbs (CFoxContextMenu)
    "FoxEnumIDList.cpp",    # IEnumIDList over a directory snapshot
    "FoxExtract.cpp",       # extract-to-temp + launch on file Open
    "FoxThumbnail.cpp",     # IThumbnailProvider for .ftex textures
    "FoxDataObject.cpp"     # IDataObject (drag-out / copy via FILECONTENTS)
) -join " "
$def = "foxshellext.def"
$dll = Join-Path $out "foxshellext.dll"

$clDll = "cl /nologo /LD /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE " +
         "/Fo:`"$out\\`" /Fe:`"$dll`" $dllSrcs " +
         "/link /DEF:$def shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib user32.lib windowscodecs.lib gdi32.lib"

# foxbrowse.exe — standalone launcher (its own translation unit).
$exe = Join-Path $out "foxbrowse.exe"
$clExe = "cl /nologo /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE " +
         "/Fo:`"$out\\`" /Fe:`"$exe`" foxbrowse.cpp"

Push-Location $here
try {
    cmd /c "`"$vcvars`" >nul 2>&1 && $clDll"
    if ($LASTEXITCODE -ne 0) { throw "foxshellext.dll build failed ($LASTEXITCODE)" }
    Write-Host "OK -> $dll"

    cmd /c "`"$vcvars`" >nul 2>&1 && $clExe"
    if ($LASTEXITCODE -ne 0) { throw "foxbrowse.exe build failed ($LASTEXITCODE)" }
    Write-Host "OK -> $exe"
} finally { Pop-Location }
